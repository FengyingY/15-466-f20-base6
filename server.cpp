
#include "Connection.hpp"

#include "glm/fwd.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>


#define PLAYER_R 0.15
#define BALL_R 0.15
#define POCKET_R 0.5
#define W 3
#define H 2
// find the closest point for moving circle c1 and static circle c2
glm::vec2 closest_point(glm::vec2 c1, glm::vec2 c2, glm::vec2 direction) {
	/*
	float slope = direction.y / direction.x;
	float x, y;
	if (slope == -1.f) {
		x = (c1.y - c2.y + c1.x - c2.x) / 2;
		y = (c1.x + c2.x + c2.y + c1.y) / 2;
	} else if (slope == 1.f) {
		x = (c2.y - c1.y + c1.x + c2.x) / 2;
		y = (c1.x - c2.x + c2.y + c1.y) / 2;
	} else {
		x = (slope * (c2.y - c1.y + slope * c1.x) - c2.x) / (slope * slope - 1);
		y = (slope * (c1.x - c2.x + slope * c2.y) - c1.y) / (slope * slope - 1);
	}
	return glm::vec2(x, y);*/
	// https://ericleong.me/research/circle-circle/#static-circle-collision
	float lx1 = c1.x, ly1 = c1.y, lx2 = c1.x+direction.x, ly2 = c1.y+direction.y, x0 = c2.x, y0 = c2.y;
	float A1 = ly2 - ly1; 
	float B1 = lx1 - lx2; 
	double C1 = (ly2 - ly1)*lx1 + (lx1 - lx2)*ly1; 
	double C2 = -B1*x0 + A1*y0; 
	double det = A1*A1 - -B1*B1; 
	double cx = 0; 
	double cy = 0; 
	if (det != 0) { 
		cx = (float)((A1*C1 - B1*C2)/det); 
		cy = (float)((A1*C2 - -B1*C1)/det); 
     } else { 
    	cx = x0; 
    	cy = y0; 
    } 
    return glm::vec2(cx, cy); 
}

// direction = end_position - start_position of circle 1
float collision_detection(glm::vec2 pos1, glm::vec2 pos2, glm::vec2 direction, float r1, float r2) {
	glm::vec2 d = closest_point(pos1, pos2, direction);
	float closest_distance = glm::pow(pos2.x - d.x, 2) + glm::pow(pos2.y - d.y, 2);

	//std::cout << "(" << pos2.x << ", " << pos2.y << ") " << d.x << ", " << d.y << " " << closest_distance << std::endl;

	if (closest_distance <= glm::pow(r1 + r2, 2)) {
		// collision detected
		float back_dist = glm::sqrt(glm::pow(r1+r2, 2) - closest_distance);
		float dist = glm::sqrt(glm::pow(pos1.x - d.x, 2) + glm::pow(pos1.y - d.y, 2));
		//std::cout << "collision detected: " << dist-back_dist << std::endl;
		return dist - back_dist;
	} else {
		return -1;
	}
}

int main(int argc, char **argv) {
#ifdef _WIN32
	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------ argument parsing ------------

	if (argc != 2) {
		std::cerr << "Usage:\n\t./server <port>" << std::endl;
		return 1;
	}

	//------------ initialization ------------

	Server server(argv[1]);


	//------------ main loop ------------
	constexpr float ServerTick = 0.10f / 10.0f; //TODO: set a server tick that makes sense for your game

	//server state:
	struct TransformState
	{
		glm::vec3 pos = glm::vec3(0, 0, 0);
		float mass = 60.f;
		glm::vec3 direction = glm::vec3(1, 0, 0);
		float speed = 1.0f;

		std::string name;
	};
	std::vector<TransformState> balls;
	
	//per-client state:
	struct PlayerInfo {
		PlayerInfo(float x, float y): pos(x, 0, 0), target_pos(y, 0, 0) {
			static uint32_t next_player_id = 1;
			name = "Player" + std::to_string(next_player_id);
			next_player_id += 1;
		}

		void reset(float x, float y, float z){
			direction = glm::vec3(0, 0, 0);
			connection = NULL;
			pos = glm::vec3(x, y, z);
			t = 0.f;
			elapsed = 0.f;
			mass = 30.f;
			speed = 3.f;
			score = 0;
		}

		std::string name;
		Connection * connection = NULL;

		glm::vec3 direction = glm::vec3(0, 0, 0);
		glm::vec3 pos = glm::vec3(0, 0, 0);
		glm::vec3 target_pos = glm::vec3(-1, -1, -1);
		float t = 0.f;
		float elapsed= 0.f;
		float mass = 30.f;
		float speed = 3.f;
		int score = 0;
		bool stay = false;
	};
	std::vector<PlayerInfo> player_info;
	player_info.emplace_back(PlayerInfo(-1.f, W));
	player_info.emplace_back(PlayerInfo(1.f, -W));

	enum game_state {Stop, OneRun, TwoRun} state;
	state = Stop;
	
	std::unordered_map< Connection *, PlayerInfo* > players;

	while (true) {
		static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration< double >(ServerTick);
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(ServerTick);
				break;
			}
			server.poll([&](Connection *c, Connection::Event evt){
				if (evt == Connection::OnOpen) {
					//client connected:

					//create some player info for them:
					for (auto & p : player_info)
					{
						if (p.connection == NULL)
						{
							p.connection = c;
							players.emplace(c, &p);
							break;
						}
					}

					if (state == Stop)
					{
						state = OneRun;
					}else if (state == OneRun)
					{
						state = TwoRun;
					}else{
						// close connection
						c->close();
					}

				} else if (evt == Connection::OnClose) {
					//client disconnected:

					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					players.erase(f);

					auto p = f->second;
					p->connection = NULL;

					if (state == OneRun)
					{
						// reset
						player_info[0].reset(-1.f, 0, 0);
						player_info[1].reset(1.f, 0, 0);
						state = Stop;
						balls.clear();
					}else if (state == TwoRun)
					{
						state = OneRun;
					}
					
				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					//std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = *f->second;

					//handle messages from client:
					//TODO: update for the sorts of messages your clients send
					while (c->recv_buffer.size() >= 5) {
						//expecting five-byte messages 'b' (left count) (right count) (down count) (up count)
						char type = c->recv_buffer[0];
						if (type != 'b') {
							//std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						int left_count = c->recv_buffer[1];
						int right_count = c->recv_buffer[2];
						int down_count = c->recv_buffer[3];
						int up_count = c->recv_buffer[4];

						player.direction = glm::vec3(right_count - left_count, up_count - down_count, 0);
						
						uint8_t size_size = c->recv_buffer[5];
						std::string size_str(&c->recv_buffer[6], size_size);
						
						size_t size;
						sscanf(size_str.c_str(), "%lu", &size);

						std::string data(&c->recv_buffer[6+size_size], size);

						std::string delimiter = ";";
						std::string ball_size_str = data.substr(0, data.find(delimiter));
						size_t ball_size = atol(ball_size_str.c_str());
						data.erase(0, data.find(delimiter) + delimiter.length());

						if (balls.size() == 0)
						{
							for (size_t i = 0; i < ball_size; i++)
							{
								balls.emplace_back();
								auto last = &balls[i];
								std::string next_ball = data.substr(0, data.find(delimiter));
								last->name = next_ball.substr(0, next_ball.find(','));
								next_ball.erase(0, next_ball.find(",")+1);
								sscanf(next_ball.c_str(),"%f,%f,%f", &last->pos.x, &last->pos.y, &last->pos.z);
								data.erase(0, data.find(delimiter) + delimiter.length());
							}
						}
						if (state != TwoRun)
						{
							c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 6 + size_size + size);
							continue;
						}
						
						std::time_t timestamp;
						float elapsed;
						//std::cout << data << "\n";
						sscanf(data.c_str(), "%lu;%f$", &timestamp, &elapsed);
						player.elapsed = elapsed;
						player.t = elapsed;
  
						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 6 + size_size + size);

						
						// boundary detection: restrict the movement via player.elapsed
						bool player_hit_boundary = false;
						float x = player.pos.x + player.direction.x * player.speed * player.elapsed;
						float y = player.pos.y + player.direction.y * player.speed * player.elapsed;
						if (x < -W + PLAYER_R) {
							player.pos.x = -W + PLAYER_R;
							player_hit_boundary = true;
						} else if (x >  W - PLAYER_R) {
							player.pos.x = W - PLAYER_R;
							player_hit_boundary = true;
						}
						if (y < -H + PLAYER_R) {
							player.pos.y = -H + PLAYER_R;
							player_hit_boundary = true;
						} else if (y > H - PLAYER_R) {
							player.pos.y = H - PLAYER_R;
							player_hit_boundary = true;
						}
						(void) player_hit_boundary;
						
/*
						// player-player collision detetion: t will be the actual moving distance for players
						if (players.size() == 2) {
							PlayerInfo *player1 = players.begin()->second;
							PlayerInfo *player2 = player1+1;
							glm::vec3 d = player1->direction * player1->elapsed * player1->speed - player2->direction * player2->elapsed * player2->speed;
							float collision_time = collision_detection(player1->pos, player2->pos, d, PLAYER_R, PLAYER_R);
							// update the 't' of each player
							if (collision_time > 0) {
								player1->t = collision_time;
								player2->t = collision_time;
							}
						}
						*/
						player.pos += player.direction * player.speed * player.elapsed;


						// player-ball collision detection: accumulate the ball's force
						// assuming that the ball will only collide with one of the player at the same time
						if (balls.size() > 0) {
							bool hit_boundary = false;
							// ball boundary detection 
							float x = balls[0].pos.x + balls[0].direction.x * balls[0].speed * elapsed;
							float y = balls[0].pos.y + balls[0].direction.y * balls[0].speed * elapsed;
							if (x < -W + BALL_R || x > W - BALL_R) {
								// update ball's position
								balls[0].pos += balls[0].direction * balls[0].speed * elapsed;
								balls[0].pos.x = x < 0 ?  2*(-W + BALL_R) - balls[0].pos.x : 2*(W - BALL_R) - balls[0].pos.x;
								balls[0].direction.x = -balls[0].direction.x;
								hit_boundary = true;
							} 
							if (y < -H + BALL_R || y > H - BALL_R) {
								// update ball's position
								balls[0].pos += balls[0].direction * balls[0].speed * elapsed;
								balls[0].pos.y = y < 0 ?  2*(-H + BALL_R) - balls[0].pos.y : 2*(H - BALL_R) - balls[0].pos.y;
								balls[0].direction.y = -balls[0].direction.y;
								hit_boundary = true;
							}

							// ball vs player
							if (!hit_boundary) {
								glm::vec3 d = player.direction * player.elapsed * player.speed - balls[0].direction * player.elapsed * balls[0].speed;
								float collision_time = collision_detection(player.pos, balls[0].pos, d, PLAYER_R, BALL_R);
								if (collision_time > 0 && collision_time < elapsed) {
									// update the ball's position
									glm::vec2 cb = balls[0].pos + balls[0].direction * balls[0].speed * collision_time;
									glm::vec2 cp = player.pos + player.direction * player.speed * collision_time;
									glm::vec2 n = glm::normalize(cb - cp);
									glm::vec2 r = glm::normalize(cb - 2 * glm::dot(cb, n) * n);
									/*
									std::cout << cb.x << ", " << cb.y << std::endl;
									std::cout << cp.x << ", " << cp.y << std::endl;
									std::cout << n.x << ", " << n.y << std::endl;
									std::cout << r.x << ", " << r.y << std::endl;
									*/
									glm::vec3 collision_point = balls[0].pos + balls[0].direction * balls[0].speed * collision_time;
									//std::cout << "before: " << balls[0].direction.x << " ";
									balls[0].direction.x = r.x;
									balls[0].direction.y = r.y;
									//std::cout << "after: " << balls[0].direction.x << std::endl;;
									balls[0].pos = collision_point + balls[0].direction * balls[0].speed * (elapsed - collision_time);
									hit_boundary = true;
								}
							}

							if (!hit_boundary) {
								balls[0].pos += balls[0].direction * balls[0].speed * elapsed;
							}

							
						}
					}
				}
			}, remain);
		}

		//update current game state
		//TODO: replace with *your* game state update

		std::string status_message = "";
		int total_score = 0;
		for (auto &[c, p] : players) {
			// send the latest position to clients 
			auto &player = *p;
			// scoring 
			if (balls.size() > 0) {
				float target_dist = glm::distance(balls[0].pos, player.target_pos);
				// std::cout << player.target_pos.x << " " << balls[0].pos.x << " " << target_dist << std::endl;
				if (!player.stay && target_dist <= POCKET_R) {
					player.stay = true;
					player.score += 1;
				}
				if (player.stay && target_dist > POCKET_R) {
					player.stay = false;
				}
			}

			(void)c; //work around "unused variable" warning on whTODOatever version of g++ github actions is running

			status_message += player.name + std::to_string(player.pos.x) + "," + 
							  std::to_string(player.pos.y) + "," + 
							  std::to_string(player.pos.z) + "|";
			
			total_score += player.score;
		}
		for (auto ball : balls) {
			status_message += ball.name + std::to_string(ball.pos.x) + "," + 
							  std::to_string(ball.pos.y) + "," + 
							  std::to_string(ball.pos.z) + "|";
		}
		
		// std::cout << status_message << std::endl; //DEBUG

		//send updated game state to all clients
		//TODO: update for your game state
		for (auto &[c, p] : players) {
			// (void)player; //work around "unused variable" warning on whatever g++ github actions uses
			auto &player = *p;
			std::string player_message = status_message + std::to_string(player.score) + "," + std::to_string(total_score - player.score);
			//send an update starting with 'm', a 24-bit size, and a blob of text:
			c->send('m');
			c->send(uint8_t(player_message.size() >> 16));
			c->send(uint8_t((player_message.size() >> 8) % 256));
			c->send(uint8_t(player_message.size() % 256));
			c->send_buffer.insert(c->send_buffer.end(), player_message.begin(), player_message.end());
		}

	}


	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}
