#include "PlayMode.hpp"
#include "LitColorTextureProgram.hpp"
#include "Mesh.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <ctime>
#include <iostream>

#include <random>
#include "Load.hpp"

GLuint pool_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > pool_meshes(LoadTagDefault, []() -> MeshBuffer const *{
	MeshBuffer const *ret = new MeshBuffer(data_path("pool.pnct"));
	pool_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > pool_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("pool.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = pool_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = pool_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

PlayMode::PlayMode(Client &client_) : client(client_), scene(*pool_scene) {
	for (auto &transform : scene.transforms)
	{
		if (std::strlen(transform.name.c_str()) > 5 && std::strncmp(transform.name.c_str(), "Ball.", 5) == 0)
		{
			balls.emplace(transform.name, &transform);
		}
		else if (std::strlen(transform.name.c_str()) == 7 && std::strncmp(transform.name.c_str(), "Player", 6) == 0)
		{
			players.emplace(transform.name, &transform);
		}
		std::cout << transform.name.c_str() << "\n" ;
	}

	camera = &scene.cameras.front();

	client.connections.back().send('b');
	client.connections.back().send(left.downs);
	client.connections.back().send(right.downs);
	client.connections.back().send(down.downs);
	client.connections.back().send(up.downs);
	std::string data;
	data += std::to_string(balls.size());
	data += ";";
	for (auto &[name, ball]:balls)
	{
		data += ball->name;
		data += ",";
		data += std::to_string(ball->position.x);
		data += ",";
		data += std::to_string(ball->position.y);
		data += ",";
		data += std::to_string(ball->position.z);
		data += ";";
	}

	std::time_t result = std::time(nullptr);
	data += std::to_string(result);
	data += ";";
	data += std::to_string(0.f);
	data += "$";

	std::cout << data.length() << "\n";
	std::cout << data << "\n";

	std::string size = std::to_string(data.length());
	Connection &c = client.connections.back();
	c.send(uint8_t(size.length()));
	c.send_buffer.insert(c.send_buffer.end(), size.begin(), size.end());
	c.send_buffer.insert(c.send_buffer.end(), data.begin(), data.end());
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	//TODO: send something that makes sense for your game
		//send a five-byte message of type 'b':
	client.connections.back().send('b');
	client.connections.back().send(left.downs);
	client.connections.back().send(right.downs);
	client.connections.back().send(down.downs);
	client.connections.back().send(up.downs);

	std::string data;
	data += std::to_string(0);
	data += ";";
	
	std::time_t result = std::time(nullptr);
	data += std::to_string(result);
	data += ";";
	data += std::to_string(elapsed);
	data += "$";

	std::string size = std::to_string(data.length());
	Connection &c = client.connections.back();
	c.send(uint8_t(size.length()));
	c.send_buffer.insert(c.send_buffer.end(), size.begin(), size.end());
	c.send_buffer.insert(c.send_buffer.end(), data.begin(), data.end());

	
	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			// std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
			//expecting message(s) like 'm' + 3-byte length + length bytes of text:
			// messages like name0=(x,y,z), ... , name15=(x,y,z), [my_score], [opponent_score]
			while (c->recv_buffer.size() >= 4) {
				// std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
				char type = c->recv_buffer[0];
				if (type != 'm') {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
				uint32_t size = (
					(uint32_t(c->recv_buffer[1]) << 16) | (uint32_t(c->recv_buffer[2]) << 8) | (uint32_t(c->recv_buffer[3]))
				);
				if (c->recv_buffer.size() < 4 + size) break; //if whole message isn't here, can't process
				//whole message *is* here, so set current server message:
				server_message = std::string(c->recv_buffer.begin() + 4, c->recv_buffer.begin() + 4 + size);
				
				std::string delimiter = "|";

				for (size_t i = 0; i < players.size(); i++)
				{
					if (server_message[0] != 'P')
					{
						break;
					}
					std::string player_str = server_message.substr(0, server_message.find(delimiter));
					std::string player_name = player_str.substr(0, 7);
					auto p = players.find(player_name);
					assert(p != players.end());
					player_str.erase(0, player_name.length());
					auto &player_transform = p->second;
					sscanf(player_str.c_str(), "%f,%f,%f", &player_transform->position.x, &player_transform->position.y, &player_transform->position.z);
					server_message.erase(0, server_message.find(delimiter)+1);
				}
				

				for (size_t i = 0; i < balls.size(); i++)
				{
					if (server_message[0] != 'B')
					{
						break;
					}
					std::string ball_str = server_message.substr(0, server_message.find(delimiter));
					std::string name = ball_str.substr(0, 7);
					auto b = balls.find(name);
					assert(b != balls.end());
					ball_str.erase(0, name.length());
					auto &ball_transform = b->second;
					sscanf(ball_str.c_str(), "%f,%f,%f", &ball_transform->position.x, &ball_transform->position.y, &ball_transform->position.z);
					server_message.erase(0, server_message.find(delimiter)+1);
				}
				
				sscanf(server_message.c_str(), "%d,%d", &my_score, &opponent_score);

				//and consume this part of the buffer:
				c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 4 + size);
			}
		}
	}, 0.0);

	// for (auto& transform: balls)
	// {
	// 	// get pos for transforms
	// 	// set pos for transforms
	// }

	// for (auto& transform: players)
	// {
	// 	// get & set pos for players
	// }

	/*
	my_score = ?;
	opponent_score = ?;
	*/

}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);
	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);	

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		auto draw_text = [&](glm::vec2 const &at, std::string const &text, float H) {
			lines.draw_text(text,
				glm::vec3(at.x, at.y, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = 2.0f / drawable_size.y;
			lines.draw_text(text,
				glm::vec3(at.x + ofs, at.y + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		};
		std::string my_score_str = "YOU : OPPONENT";
		std::string op_score_str = std::to_string(my_score) + " : " + std::to_string(opponent_score);

		draw_text(glm::vec2(-aspect + 0.1f, 0.1f), my_score_str, 0.09f);
		draw_text(glm::vec2(-aspect + 0.2f, -0.1f), op_score_str, 0.09f);

		draw_text(glm::vec2(-aspect + 0.1f,-0.9f), "(press WASD to change your total)", 0.09f);
	}
	GL_ERRORS();
}
