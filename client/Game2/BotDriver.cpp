
// BotDriver.cpp
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

#include <math.h>

#include "../../engine/MainCharacter/MainCharacter.h"
#include "../../engine/Model/Level.h"

#include "BotDriver.h"

namespace HoverRace {
namespace Client {

namespace {

/**
 * How far past the doorway to aim, as a fraction of the way to the next
 * room's centre. Aiming exactly at the doorway makes the bot clip its edge;
 * aiming well past it carries the craft cleanly through.
 */
const double PORTAL_OVERSHOOT = 0.55;

/// Below this heading error the bot holds its line rather than sawing at it.
const MR_Angle STEER_DEADBAND = 24;

/// Above this heading error the corner is too tight to take at full speed.
const MR_Angle BRAKE_THRESHOLD = 430;

/// Centre of a room, in track coordinates.
MR_2DCoordinate RoomCentroid(const Model::Level &level, int room)
{
	const int count = level.GetRoomVertexCount(room);
	MR_Int64 sumX = 0;
	MR_Int64 sumY = 0;

	for (int i = 0; i < count; i++) {
		const MR_2DCoordinate &v = level.GetRoomVertex(room, i);
		sumX += v.mX;
		sumY += v.mY;
	}

	MR_2DCoordinate retv;
	retv.mX = static_cast<MR_Int32>(count ? sumX / count : 0);
	retv.mY = static_cast<MR_Int32>(count ? sumY / count : 0);
	return retv;
}

}  // namespace

BotDriver::BotDriver(std::shared_ptr<MainCharacter::MainCharacter> craft,
	double skill) :
	craft(std::move(craft)), skill(skill), targetRoom(0)
{
}

BotDriver::~BotDriver()
{
}

void BotDriver::Update(const Model::Level &level)
{
	if (!craft) return;

	const int roomCount = level.GetRoomCount();
	if (roomCount <= 0) return;

	// Rooms are stored in circuit order, so "further along the track" is just
	// a higher index. Re-deriving the target from the craft's current room
	// each frame means a bot that gets shoved backwards recovers on its own,
	// instead of driving at a target it has already passed.
	const int here = (craft->mRoom >= 0 && craft->mRoom < roomCount) ?
		craft->mRoom : 0;
	targetRoom = (here + 1) % roomCount;

	// Steer through the doorway, not at the next room's centre.
	//
	// Rooms are convex but the track is not, so a straight line to the next
	// centre -- and worse, to a centre two rooms away -- can pass clean
	// through a solid wall. The bots pinned themselves against the outside
	// wall on the first bend doing exactly that. A wall is only solid where
	// the room has no neighbour, so the shared edge with the next room is a
	// guaranteed-traversable gap; aim there, then a little beyond it.
	const int vertexCount = level.GetRoomVertexCount(here);
	MR_2DCoordinate aim = RoomCentroid(level, targetRoom);

	for (int i = 0; i < vertexCount; i++) {
		if (level.GetNeighbor(here, i) != targetRoom) continue;

		const MR_2DCoordinate &a = level.GetRoomVertex(here, i);
		const MR_2DCoordinate &b =
			level.GetRoomVertex(here, (i + 1) % vertexCount);

		const double gateX = (a.mX + b.mX) / 2.0;
		const double gateY = (a.mY + b.mY) / 2.0;
		const MR_2DCoordinate beyond = RoomCentroid(level, targetRoom);

		aim.mX = static_cast<MR_Int32>(
			gateX + (beyond.mX - gateX) * PORTAL_OVERSHOOT);
		aim.mY = static_cast<MR_Int32>(
			gateY + (beyond.mY - gateY) * PORTAL_OVERSHOOT);
		break;
	}

	const double dx = static_cast<double>(aim.mX - craft->mPosition.mX);
	const double dy = static_cast<double>(aim.mY - craft->mPosition.mY);
	if (fabs(dx) < 1.0 && fabs(dy) < 1.0) return;

	// Orientation 0 points along +X: the simulation drives the craft with
	// MR_Cos[orientation] on X and MR_Sin[orientation] on Y.
	const MR_Angle desired = RAD_2_MR_ANGLE(atan2(dy, dx));

	// Signed error in -MR_PI..MR_PI, so the bot always turns the short way.
	int error = static_cast<int>(MR_NORMALIZE_ANGLE(desired - craft->mOrientation));
	if (error > MR_PI) error -= MR_2PI;

	// Steering. Right decreases the angle, left increases it.
	const MR_Angle deadband =
		static_cast<MR_Angle>(STEER_DEADBAND / (skill > 0.1 ? skill : 0.1));
	const bool turnLeft = error > deadband;
	const bool turnRight = error < -deadband;

	craft->SetTurnLeftState(turnLeft);
	craft->SetTurnRightState(turnRight);

	// Throttle. Lifting off through the sharp stuff is what stops the bot
	// understeering into the outside wall on every hairpin; a less skilled
	// bot leaves it later and handles corners worse.
	const int brakePoint =
		static_cast<int>(BRAKE_THRESHOLD * (2.0 - skill));
	const bool tightCorner = abs(error) > brakePoint;

	craft->SetEngineState(!tightCorner);
	craft->SetBrakeState(tightCorner && abs(error) > brakePoint * 3 / 2);
}

}  // namespace Client
}  // namespace HoverRace
