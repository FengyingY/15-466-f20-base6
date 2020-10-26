
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
	constexpr float ServerTick = 1.0f / 10.0f; //TODO: set a server tick that makes sense for your game

	//server state:
	struct TransformState
	{
		glm::vec3 pos = glm::vec3(0, 0, 0);
		float mass = 60.f;
		glm::vec2 force;
		glm::vec2 speed;

		std::string name;
	};
	std::vector<TransformState> balls;
	
	//per-client state:
	struct PlayerInfo {
		PlayerInfo() {
			static uint32_t next_player_id = 1;
			name = "Player" + std::to_string(next_player_id);
			next_player_id += 1;
		}
		std::string name;

		uint32_t left_presses = 0;
		uint32_t right_presses = 0;
		uint32_t up_presses = 0;
		uint32_t down_presses = 0;

		glm::vec3 pos = glm::vec3(0, 0, 0);
		float t;
		float elapsed;
		float mass = 30.f;
		float speed = 30.f;
		int score = 0;
	};
	std::unordered_map< Connection *, PlayerInfo > players;

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
					PlayerInfo new_player = PlayerInfo();
					// first player
					if (players.size() == 0) {
						new_player.pos.x = -1.f;
					} else { // second player
						new_player.pos.x = 1.f;
					}
					players.emplace(c, new_player);


				} else if (evt == Connection::OnClose) {
					//client disconnected:

					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					players.erase(f);


				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = f->second;

					//handle messages from client:
					//TODO: update for the sorts of messages your clients send
					while (c->recv_buffer.size() >= 5) {
						//expecting five-byte messages 'b' (left count) (right count) (down count) (up count)
						char type = c->recv_buffer[0];
						if (type != 'b') {
							std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						uint8_t left_count = c->recv_buffer[1];
						uint8_t right_count = c->recv_buffer[2];
						uint8_t down_count = c->recv_buffer[3];
						uint8_t up_count = c->recv_buffer[4];

						player.left_presses += left_count;
						player.right_presses += right_count;
						player.down_presses += down_count;
						player.up_presses += up_count;
						
						uint8_t size_size = c->recv_buffer[5];
						std::string size_str(&c->recv_buffer[6], size_size);
						
						size_t size; // TODO always fail？？？
						sscanf(size_str.c_str(), "%lu", &size);
						std::string data(&c->recv_buffer[6+size_size], size);

						std::string delimiter = ";";
						std::string ball_size_str = data.substr(0, data.find(delimiter));
						size_t ball_size = atol(ball_size_str.c_str());
						data.erase(0, data.find(delimiter) + delimiter.length());

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

						std::time_t timestamp;
						float elapsed;
						std::cout << data << "\n";
						sscanf(data.c_str(), "%lu;%f$", &timestamp, &elapsed);
						player.elapsed = elapsed;
						player.t = elapsed;
  
						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 6 + size_size + size);
					}
				}
			}, remain);
		}

		//update current game state
		//TODO: replace with *your* game state update
		
		// player-player collision detetion
		// update the 't' of each player
		

		// player-ball collision detection
		// 1. accumulate the ball's force 
		// 2. update the player's t if necessary 
			

		std::string status_message = "";
		int total_score = 0;
		for (auto &[c, player] : players) {
			// TODO  update the position with t of players and ball's force

			// send the latest position to clients 
			glm::vec3 dir = glm::vec3(player.right_presses - player.left_presses, player.up_presses - player.down_presses, 0);
			player.pos += player.t * dir * player.speed;

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
		//std::cout << status_message << std::endl; //DEBUG

		//send updated game state to all clients
		//TODO: update for your game state
		for (auto &[c, player] : players) {
			(void)player; //work around "unused variable" warning on whatever g++ github actions uses
			status_message += std::to_string(player.score) + "|" + std::to_string(total_score - player.score);
			//send an update starting with 'm', a 24-bit size, and a blob of text:
			c->send('m');
			c->send(uint8_t(status_message.size() >> 16));
			c->send(uint8_t((status_message.size() >> 8) % 256));
			c->send(uint8_t(status_message.size() % 256));
			c->send_buffer.insert(c->send_buffer.end(), status_message.begin(), status_message.end());
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
