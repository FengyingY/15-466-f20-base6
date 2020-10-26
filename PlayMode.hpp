#include "Mode.hpp"

#include "Connection.hpp"
#include "Scene.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0; 
	} left, right, down, up;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;
	Scene scene;

	std::vector<Scene::Transform*> balls;
	Scene::Camera *camera = nullptr;
	
	std::vector<Scene::Transform*> players;

	unsigned int my_score;
	unsigned int opponent_score;

};
