
// BotDriver.h
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.

#pragma once

#include <memory>

namespace HoverRace {
	namespace MainCharacter {
		class MainCharacter;
	}
	namespace Model {
		class Level;
	}
}

namespace HoverRace {
namespace Client {

/**
 * Drives one computer-controlled craft around the track.
 *
 * Tracks are stored as convex rooms in circuit order, so the sequence of room
 * centroids already describes a lap. The bot just steers toward a centroid a
 * few rooms ahead and holds the throttle -- no navmesh or path search needed,
 * and it works on any track the game can load, including user-made ones.
 *
 * The craft itself is an ordinary MainCharacter inserted into the level, so it
 * collides, bounces and is rendered exactly like a human racer. The only
 * difference is where its control inputs come from.
 */
class BotDriver
{
public:
	BotDriver(std::shared_ptr<MainCharacter::MainCharacter> craft,
		double skill = 1.0);
	~BotDriver();

	/// Choose this frame's control inputs. Call once per simulation step.
	void Update(const Model::Level &level);

	std::shared_ptr<MainCharacter::MainCharacter> GetCraft() const {
		return craft;
	}

private:
	std::shared_ptr<MainCharacter::MainCharacter> craft;

	/// 0..1. Scales cornering discipline and how early the bot lifts off.
	double skill;

	/// Room the bot is currently steering toward.
	int targetRoom;
};

}  // namespace Client
}  // namespace HoverRace
