
// MainCharacter.cpp
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
// Copyright (c) 2013-2015 Michael Imamura.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.
//

#include <math.h>

#include "MainCharacterRenderer.h"
#include "MainCharacter.h"
#include "../Model/Track.h"
#include "../Model/RaceEffects.h"
#include "../Model/ObstacleCollisionReport.h"
#include "../Util/BitPacking.h"
#include "../Util/Log.h"
#include "../Exception.h"

using namespace HoverRace::Util;
using namespace HoverRace::VideoServices;

namespace HoverRace {
namespace MainCharacter {

#define MR_NB_HOVER_MODEL 8

using MainCharacterState = Util::BitPack<23>;

// Packing description
//                     Offset  len   Prec
#define  MC_POSX           0,   32,     5
#define  MC_POSY          32,   32,     5
#define  MC_POSZ          64,   27,     0
#define  MC_ROOM          91,   11,     0
#define  MC_ORIENTATION  102,    9,     3
#define  MC_SPEED_X_256  111,   17,     2
#define  MC_SPEED_Y_256  128,   17,     2
#define  MC_SPEED_Z_256  145,    9,     2
#define  MC_CONTROL_ST   154,   15,     0
#define  MC_ON_FLOOR     169,    1,     0
#define  MC_HOVER_MODEL  170,    3,     0
#define  MC_PADDING      173,   11,     0
//   #define  MC_SOUNDFX      141,    5,     0
// Total                 184  = 23 bytes

static_assert(std::is_pod<MainCharacterState>::value,
	"MainCharacterState must be a POD type");

// Local constants
//
// MINIMUM_SPLITTABLE_TIME_SLICE must stay BELOW TIME_SLICE.
//
// Simulate() only ever hands InternalSimulate() slices of at most TIME_SLICE,
// so a minimum of 6 made the "halve the step and retry" loop in
// InternalSimulate() unreachable -- lDuration started at 5 and the loop broke
// on its first pass, every time. A blocked move then produced no movement at
// all instead of the largest movement that fits, which is what makes the craft
// stick to walls rather than creep up to them.
#define TIME_SLICE                     5
#define MINIMUM_SPLITTABLE_TIME_SLICE  2

const int eCharacterMovementRay = 1100;
const int eCharacterRay = 1300;
const int eCharacterHeight = 1500;
const int eCharacterContactRay = 1450;
//                             Basic  CX  BiTurbo  Eon  Nova
const int eCharacterWeight[MR_NB_HOVER_MODEL] =
	{ 300, 250, 450, 300, 200, 300, 300, 300 };
const int eMissileRefillTime = 10000;
const int ePwrUpDuration = 5000;

const double eSteadySpeed[MR_NB_HOVER_MODEL] =
{
	8.7 * 2222.0 / 1000.0,						  // 0: Basic
	11.5 * 2222.0 / 1000.0,						  // 1: CX
	10.5 * 2222.0 / 1000.0,						  // 2: Bi-Turbo
	8.7 * 2222.0 / 1000.0,						  // 3: Eon
	12.6 * 2222.0 / 1000.0,						  // 4: Nova - fastest flat out
	8.7 * 2222.0 / 1000.0,
	8.7 * 2222.0 / 1000.0,
	8.1 * 2222.0 / 1000.0
};
const double eMaxZSpeed[MR_NB_HOVER_MODEL] =
{
	2900.0 / 1000.0,
	2900.0 / 900.0,
	2900.0 / 1000.0,
	2900.0 / 1000.0,
	2900.0 / 1000.0,
	2900.0 / 1000.0,
	2900.0 / 1000.0,
	2900.0 / 1000.0
};

const double eRotationSpeed = (MR_PI / 1.4) / 1000.0;

const double eFrictionAccell[MR_NB_HOVER_MODEL] =
{
	-eSteadySpeed[0] / 4.0 / 1000.0,			  // 0: Basic
	-eSteadySpeed[0] / 4.0 / 1200.0,			  // 1: CX
	-eSteadySpeed[0] / 4.0 / 900.0,				  // 2: Bi-Turbo
	-eSteadySpeed[0] / 4.0 / 1000.0,			  // 3: Eon
	-eSteadySpeed[0] / 4.0 / 1600.0,			  // 4: Nova - glides, bleeds speed slowly
	-eSteadySpeed[0] / 4.0 / 1000.0,
	-eSteadySpeed[0] / 4.0 / 1000.0,
	-eSteadySpeed[0] / 4.0 / 700.0
};

const double eMotorAccell[MR_NB_HOVER_MODEL] =
{
	eSteadySpeed[0] / 1000.0,					  // 0: Basic
	eSteadySpeed[0] / 1400.0,					  // 1: CX
	eSteadySpeed[2] / 1050.0,					  // 2: Bi-Turbo
	eSteadySpeed[0] / 1000.0,					  // 3: Eon
	eSteadySpeed[0] / 1700.0,					  // 4: Nova - slow to wind up
	eSteadySpeed[0] / 1000.0,
	eSteadySpeed[0] / 1000.0,
	eSteadySpeed[7] / 750.0
};

const double eZAccell[MR_NB_HOVER_MODEL] =
{
	-eMaxZSpeed[0] / 1000.0,
	-eMaxZSpeed[1] / 1000.0,
	-eMaxZSpeed[2] / 850.0,
	-eMaxZSpeed[0] / 1000.0,
	-eMaxZSpeed[0] / 1000.0,
	-eMaxZSpeed[0] / 1000.0,
	-eMaxZSpeed[0] / 1000.0,
	-eMaxZSpeed[7] / 1000.0
};

// const double eBreakAccell             = -eSteadySpeed*3.0/1000.0;
// const double eFrictionRotationAccell  = -eSteadyRotationSpeed*6.0/1000.0;
// const double eBreakRotationSpeedAccell= -eSteadyRotationSpeed*12.0/1000.0;
// const double eJetRotationSpeedAccell  =  eSteadyRotationSpeed*12.0/1000.0;

const double eFuelCapacity = 3 * 60 * 1000;		  // 3 minutes of fuel
const double eFuelConsuming[MR_NB_HOVER_MODEL] =
{
	1.0,										  // 0: Basic
	0.70,										  // 1: CX
	2.0,										  // 2: Bi-Turbo
	1.0,										  // 3: Eon
	1.55,										  // 4: Nova - thirsty
	1.0,
	1.0,
	1.1,
};

namespace {

const Util::ObjectFromFactoryId MAINCHAR_ID = { 5000, 1 };

/// An invalid game config slipped through.
class TrackConfigExn : public Exception
{
	using SUPER = Exception;

public:
	TrackConfigExn() : SUPER() { }
	TrackConfigExn(const std::string &msg) : SUPER(msg) { }
	TrackConfigExn(const char *msg) : SUPER(msg) { }
	virtual ~TrackConfigExn() throw() { }
};

/// Number of selectable craft, including those with no legacy flag bit.
const unsigned int eCraftCount = 5;

/// Craft that predate the legacy "gameOpts" flags and so are always available.
const unsigned int eFirstUnflaggedCraft = 4;

/**
 * Check whether a craft may be selected.
 *
 * The legacy gameOpts byte only has four craft bits (0x01 - 0x08); 0x10 is
 * already OPT_ALLOW_CANS. Craft added beyond those four therefore have no bit
 * to test and are always selectable, rather than silently colliding with an
 * unrelated flag.
 *
 * @param gameOpts The current game options.
 * @param craft The craft ID.
 * @return Whether the craft can be chosen.
 */
bool IsCraftSelectable(char gameOpts, unsigned int craft)
{
	if (craft >= eFirstUnflaggedCraft) return true;
	return (gameOpts & (1 << craft)) != 0;
}

/**
 * Find the next craft that's allowed by the config.
 * @param gameOpts The current game options.
 * @param curCraft The currently-selected craft ID.
 * @param step The direction to search (1 = forwards, -1 backwards).
 * @return The next allowed craft ID.
 */
unsigned int NextAllowedCraft(char gameOpts, unsigned int curCraft,
	int step = 1)
{
	if ((gameOpts & 0x0f) == 0) {
		throw TrackConfigExn("All crafts have been disabled");
	}

	do {
		curCraft = static_cast<unsigned int>(
			(static_cast<int>(curCraft) + step + eCraftCount) % eCraftCount);
	} while (!IsCraftSelectable(gameOpts, curCraft));

	return curCraft;
}

}  // namespace

// Functions implementations

MainCharacter::MainCharacter() :
	Model::FreeElement(MAINCHAR_ID),
	playerIdx(0), started(false), finished(false)
{
	mMasterMode = TRUE;
	mRoom = -1;
	mHoverModel = 0;							  // Basic model
	mNetPriority = FALSE;
	mLastCollisionTime = 0;

	mControlState = 0;
	mMotorOnState = FALSE;
	mMotorDisplay = 0;

	mXSpeed = 0;
	mYSpeed = 0;
	mZSpeed = 0;
	mXSpeedBeforeCollision = 0;
	mYSpeedBeforeCollision = 0;
	mOnFloor = FALSE;
	mCabinOrientation = 0;						  // mOrientation; mOrientation is not set yet
	mOutOfControlDuration = 0;
	mMissileRefillDuration = 0;

	mFireDone = TRUE;

	mCurrentWeapon = eMissile;
	mPowerUpLeft = 0;

	mFuelLevel = eFuelCapacity;

	mCollisionShape.mRay = eCharacterRay;
	mContactShape.mRay = eCharacterContactRay;

	mContactEffectList.push_back(&mContactEffect);

	mLastLapCompletion = 0;
	mLastLapDuration = 0;

	mHoverId = 10;

	mCheckPoint1 = FALSE;
	mCheckPoint2 = FALSE;

}

MainCharacter::~MainCharacter()
{
}

void MainCharacter::SetAsMaster()
{
	mMasterMode = TRUE;
}

void MainCharacter::SetAsSlave()
{
	mMasterMode = FALSE;
}

void MainCharacter::SetHoverId(int pId)
{
	mHoverId = pId;
}

void MainCharacter::SetHoverModel(unsigned int pModel)
{
	mHoverModel = pModel;
}

unsigned int MainCharacter::GetHoverModel() const
{
	return mHoverModel;
}

void MainCharacter::SetOrientation(MR_Angle pOrientation)
{
	mOrientation = pOrientation;
	mCabinOrientation = pOrientation;
}

int MainCharacter::GetHoverId() const
{
	return mHoverId;
}

void MainCharacter::AddRenderer()
{
	if (!mRenderer) {
		Util::ObjectFromFactoryId lId = { 1, 100 };
		mRenderer = std::dynamic_pointer_cast<MainCharacterRenderer>(
			Util::DllObjectFactory::CreateObject(lId));
	}
}

void MainCharacter::Render(VideoServices::Viewport3D * pDest, MR_SimulationTime /*pTime */ )
{
	if (mRenderer)
		mRenderer->Render(pDest, mPosition, mCabinOrientation, mMotorDisplay > 0, mHoverId, mHoverModel);
}

/**
 * Create a new player.
 * @param idx The player index (starting at 0 for player 1).
 * @param gameopts The game options.
 */
MainCharacter *MainCharacter::New(int idx, char gameopts)
{
	MainCharacter *lReturnValue = new MainCharacter();

	if (lReturnValue) {
		lReturnValue->playerIdx = idx;
		lReturnValue->mGameOpts = gameopts;
		lReturnValue->mHoverModel = NextAllowedCraft(gameopts, 3);
	}

	return lReturnValue;
}

Model::ElementNetState MainCharacter::GetNetState() const
{
	static MainCharacterState lsState;		  // Static is ok because the variable will be used immediatly

	Model::ElementNetState lReturnValue;

	lReturnValue.mDataLen = lsState.SIZE;
	lReturnValue.mData = (MR_UInt8 *) &lsState;

	lsState.Clear();

	lsState.Set(MC_POSX, mPosition.mX);
	lsState.Set(MC_POSY, mPosition.mY);
	lsState.Set(MC_POSZ, mPosition.mZ);

	lsState.Set(MC_ROOM, mRoom);

	lsState.Set(MC_ORIENTATION, mOrientation);

	lsState.Set(MC_SPEED_X_256, static_cast<unsigned int>(mXSpeedBeforeCollision * 256));
	lsState.Set(MC_SPEED_Y_256, static_cast<unsigned int>(mYSpeedBeforeCollision * 256));
	lsState.Set(MC_SPEED_Z_256, static_cast<unsigned int>(mZSpeed * 256));

	lsState.Set(MC_CONTROL_ST, mControlState);
	lsState.Set(MC_ON_FLOOR, mOnFloor);
	lsState.Set(MC_HOVER_MODEL, mHoverModel);
	lsState.Set(MC_PADDING, 0);

	/*
	   lsState.mPosX          = mPosition.mX;
	   lsState.mPosY          = mPosition.mY;
	   lsState.mPosZ          = mPosition.mZ;

	   // lsState.mRoom          = mRoom;

	   lsState.mOrientation   = mOrientation;

	   lsState.mXSpeed_256    = mXSpeed*256;
	   lsState.mYSpeed_256    = mYSpeed*256;
	   lsState.mZSpeed_256    = mZSpeed*256;

	   lsState.mControlState  = mControlState;

	   lsState.mOnFloor       = mOnFloor;
	 */
	return lReturnValue;
}

void MainCharacter::SetNetState(int /*pDataLen */ , const MR_UInt8 *pData)
{
	MainCharacterState lState;
	lState.InitFrom(pData);

	mPosition.mX = lState.Get(MC_POSX);
	mPosition.mY = lState.Get(MC_POSY);
	mPosition.mZ = lState.Get(MC_POSZ);

	mRoom = lState.Get(MC_ROOM);

	if(mRoom < -1)
		mRoom = lState.Get(MC_ROOM);

	mOrientation = static_cast<MR_Angle>(lState.Getu(MC_ORIENTATION));

	mXSpeed = lState.Get(MC_SPEED_X_256) / 256.0;
	mYSpeed = lState.Get(MC_SPEED_Y_256) / 256.0;
	mZSpeed = lState.Get(MC_SPEED_Z_256) / 256.0;

	mControlState = lState.Getu(MC_CONTROL_ST);
	mOnFloor = lState.Get(MC_ON_FLOOR);
	mHoverModel = lState.Getu(MC_HOVER_MODEL);

	/*
	   mPosition.mX = lState->mPosX;
	   mPosition.mY = lState->mPosY;
	   mPosition.mZ = lState->mPosZ;

	   // mRoom        = lState->mRoom;

	   mOrientation = lState->mOrientation;

	   mXSpeed      = lState->mXSpeed_256/256.0;
	   mYSpeed      = lState->mYSpeed_256/256.0;
	   mZSpeed      = lState->mZSpeed_256/256.0;

	   mControlState= lState->mControlState;

	   mOnFloor     = lState->mOnFloor;
	 */

	// Determine cabin orientation
	if(mControlState & eBreakDirection)
		mCabinOrientation = MR_NORMALIZE_ANGLE(RAD_2_MR_ANGLE(atan2(mYSpeed, mXSpeed)) + MR_PI);
	//else if( (mControlState & eStraffleRight)^(mControlState & eStraffleLeft)) {
	//      if(mControlState & eStraffleRight)
	//              mCabinOrientation = MR_NORMALIZE_ANGLE(mOrientation - MR_PI / 2);
	//      else
	//              mCabinOrientation = MR_NORMALIZE_ANGLE(mOrientation + MR_PI / 2);
	//}
}

void MainCharacter::SetSimulationTime(MR_SimulationTime pTime)
{
	mCurrentTime = pTime;
}

void MainCharacter::SetEngineState(bool engineState) {
	if(mFuelLevel <= 0.0) { // if the user is out of fuel... they're going nowhere
		if(!mMotorOnState && engineState)
			mFuelLevel = 120; // gives player a tiny bit of fuel so they can limp to a pit
		else
			engineState = false;
	}
	mMotorOnState = engineState;
}

void MainCharacter::SetTurnLeftState(bool leftState)
{
	if(leftState) {
		if(mCurrentTime < 0) {
			mHoverModel = NextAllowedCraft(mGameOpts, mHoverModel, -1);
		}
		mControlState |= eLeft;
	} else
		mControlState &= ~eLeft;
}

void MainCharacter::SetTurnRightState(bool rightState)
{
	if(rightState) {
		if(mCurrentTime < 0) {
			mHoverModel = NextAllowedCraft(mGameOpts, mHoverModel);
		}

		mControlState |= eRight;
	} else
		mControlState &= ~eRight;
}

void MainCharacter::SetJump()
{
	if(!(mControlState & eJump)) {
		if(mOnFloor) {
			mZSpeed = 1.1 * eMaxZSpeed[mHoverModel];
			if (mRenderer)
				mInternalSoundList.Add(mRenderer->GetJumpSound());
		}
		else {
			if (mRenderer)
				mInternalSoundList.Add(mRenderer->GetMisJumpSound());
		}
	}
}

void MainCharacter::SetPowerup()
{
	if(mFireDone)
		mFireDone = FALSE;
}

void MainCharacter::SetChangeItem()
{
	if(!(mControlState & eSelectWeapon)) {
		mCurrentWeapon = static_cast<eWeapon>(
			(static_cast<int>(mCurrentWeapon) + 1) % eNotAWeapon);
	}
}

void MainCharacter::SetBrakeState(bool brakeState)
{
	if(brakeState)
		mControlState |= eBreakDirection;
	else
		mControlState &= ~eBreakDirection;
}

void MainCharacter::SetLookBackState(bool lookBackState)
{
	if(lookBackState)
		mControlState |= eLookBack;
	else {
		mControlState &= ~eLookBack;
		mOrientation = mCabinOrientation; // reset orientation
	}
}

int MainCharacter::Simulate(MR_SimulationTime pDuration,
	Model::Track &track, int pRoom)
{
	mRoom = pRoom;
	auto *level = track.GetLevel();

	if(pDuration > 0) {
		if(mMasterMode) {
			if (!started) {
				started = true;
				startedSignal(this);
			}
			if((mMotorOnState) && (mFuelLevel > 0.0))
				mMotorDisplay = 250;
		}
		else {
			if(mMotorOnState)
				mMotorDisplay = 250;
		}
	}

	mMotorDisplay -= pDuration;

	if(mMotorDisplay < 0)
		mMotorDisplay = 0;

	// Orient the cabin if a special move have been made
	if(mControlState & eBreakDirection) {
		double lAbsoluteSpeed = sqrt(mXSpeed * mXSpeed + mYSpeed * mYSpeed);
		if(lAbsoluteSpeed > (eSteadySpeed[mHoverModel] / 20.0))
			mCabinOrientation = MR_NORMALIZE_ANGLE(RAD_2_MR_ANGLE(atan2(mYSpeed, mXSpeed)) + MR_PI);
	}
	else if(mControlState & eLookBack) {
		if(mCabinOrientation - mOrientation < (MR_PI / 2))
			mOrientation = MR_NORMALIZE_ANGLE(mCabinOrientation - MR_PI);
	}
	//else if((mControlState & eStraffleRight) ^ (mControlState & eStraffleLeft)) {
	//      if(mControlState & eStraffleRight)
	//              mCabinOrientation = MR_NORMALIZE_ANGLE(mOrientation - MR_PI / 2);
	//      else
	//              mCabinOrientation = MR_NORMALIZE_ANGLE(mOrientation + MR_PI / 2);
	//}
	else
		mCabinOrientation = mOrientation;

	if(mMasterMode) {
		MR_SimulationTime lDuration = pDuration;

		while(lDuration > 0) {
			if(lDuration > TIME_SLICE)
				pRoom = InternalSimulate(TIME_SLICE, track, pRoom);
			else
				pRoom = InternalSimulate(lDuration, track, pRoom);
			lDuration -= TIME_SLICE;
		}

		mXSpeedBeforeCollision = mXSpeed;
		mYSpeedBeforeCollision = mYSpeed;

		// If the user pressed fire, launch a missile
		mMissileRefillDuration -= pDuration;
		if(mMissileRefillDuration <= 0)
			mMissileRefillDuration = 0;

		mPowerUpLeft -= static_cast<MR_SimulationTime>(pDuration * eFuelConsuming[mHoverModel]);
		if(mPowerUpLeft < 0)
			mPowerUpLeft = 0;

		if(!mFireDone) {
			mFireDone = TRUE;

			if(mCurrentWeapon == eMissile) {
				if((mMissileRefillDuration == 0) && (mGameOpts & OPT_ALLOW_WEAPONS)) {
					mMissileRefillDuration = eMissileRefillTime;

					Util::ObjectFromFactoryId lObjectId = { 1, 150 };
					// Create a new missile
					auto lMissile = std::dynamic_pointer_cast<FreeElement>(
						Util::DllObjectFactory::CreateObject(lObjectId));

					if (lMissile) {
						lMissile->SetOwnerId(mHoverId);
						lMissile->mPosition = mPosition;
						lMissile->mPosition.mZ += 1100;
						lMissile->mOrientation = mCabinOrientation;

						level->InsertElement(lMissile, mRoom, TRUE);

						if (mRenderer) {
							mInternalSoundList.Add(mRenderer->GetFireSound());
							mExternalSoundList.Add(mRenderer->GetFireSound());
						}
					}
				}
			}
			else if(mCurrentWeapon == eMine) {
				if(!mMineList.IsEmpty() && (mGameOpts & OPT_ALLOW_MINES)) {
					MR_3DCoordinate lPos = mPosition;
					lPos.mZ += 800;
					level->SetPermElementPos(mMineList.GetHead(), mRoom, lPos);
					mMineList.Remove();
				}
			}
			else if(mCurrentWeapon == ePowerUp) {
				if(!mPowerUpList.IsEmpty() && (mGameOpts & OPT_ALLOW_CANS)) {
					MR_3DCoordinate lPos = mPosition;
					lPos.mZ += 1200;

					level->SetPermElementPos(mPowerUpList.GetHead(), mRoom, lPos);
					mPowerUpList.Remove();

					mPowerUpLeft = ePwrUpDuration;
				}
			}
		}
	} else									  // Slave mode
		pRoom = InternalSimulate(pDuration, track, pRoom);

	return pRoom;
}

int MainCharacter::InternalSimulate(MR_SimulationTime pDuration,
	Model::Track &track, int pRoom)
{
	auto level = track.GetLevel();

	// Determine new speed (PosVar and OrientationVar
	double lAbsoluteSpeed = sqrt(mXSpeed * mXSpeed + mYSpeed * mYSpeed);

	if(mOutOfControlDuration <= 0) {		  // No friction when out of control..it's just more cool
		if(lAbsoluteSpeed <= -pDuration * eFrictionAccell[mHoverModel]) {
			mXSpeed = 0;
			mYSpeed = 0;
		}
		else {
			double lFrictionAmplifier = 1.0;

			if(lAbsoluteSpeed < eSteadySpeed[mHoverModel] / 3)
				lFrictionAmplifier = 0.4;
			else if((lAbsoluteSpeed > eSteadySpeed[mHoverModel]) && (lAbsoluteSpeed < 2.5 * eSteadySpeed[mHoverModel]))
				lFrictionAmplifier = std::min(1.7, 1.1 + 2.5 * (lAbsoluteSpeed / eSteadySpeed[0] - 1.0));

			double lConstantPart = pDuration * lFrictionAmplifier * eFrictionAccell[mHoverModel] / lAbsoluteSpeed;

			mXSpeed += lConstantPart * mXSpeed;
			mYSpeed += lConstantPart * mYSpeed;
		}
	}

	mZSpeed += (pDuration * eZAccell[mHoverModel]) * track.GetGravity();

	if(mZSpeed < -eMaxZSpeed[mHoverModel])
		mZSpeed = -eMaxZSpeed[mHoverModel];

	// Apply rotation
	if(mOutOfControlDuration > 0) {
		mOutOfControlDuration -= pDuration;

		mOrientation = MR_NORMALIZE_ANGLE((int) (mOrientation + pDuration * 8 * eRotationSpeed));
	}
	else {
		if((mControlState & eRight) ^ (mControlState & eLeft)) {
			double lRotation;

			if(mControlState & eRight)
				lRotation = -pDuration * eRotationSpeed;
			else
				lRotation = pDuration * eRotationSpeed;

			if(mControlState & eSlowRotation)
				lRotation /= 4;

			if(mControlState & eLookBack)
				mCabinOrientation = MR_NORMALIZE_ANGLE(mCabinOrientation + (int) lRotation);
			else
				mOrientation = MR_NORMALIZE_ANGLE(mOrientation + (int) lRotation);
		}
	}

	// MotorEffect
	if(mMotorOnState) {
		double lDirectionalSpeed = (mXSpeed * MR_Cos[mCabinOrientation] + mYSpeed * MR_Sin[mCabinOrientation]) / MR_TRIGO_FRACT;

		double lMaxSpeedFactor = 1.3;

		if(mPowerUpLeft > 0)
			lMaxSpeedFactor = 1.9;

		if(lDirectionalSpeed < (lMaxSpeedFactor * eSteadySpeed[mHoverModel])) {
			double lAccelerationFactor = 1.0 - lDirectionalSpeed / (lMaxSpeedFactor * 1.25 * eSteadySpeed[mHoverModel]);

			if(lAccelerationFactor < 0)
				lAccelerationFactor = 0;
			else if(lAccelerationFactor > 0.8)
				lAccelerationFactor = 0.8;

			if(mPowerUpLeft > 0)
				lAccelerationFactor *= 3.2;
			else
				lAccelerationFactor *= 1.8;

			mXSpeed += (pDuration * lAccelerationFactor * eMotorAccell[mHoverModel] * MR_Cos[mCabinOrientation]) / MR_TRIGO_FRACT;
			mYSpeed += (pDuration * lAccelerationFactor * eMotorAccell[mHoverModel] * MR_Sin[mCabinOrientation]) / MR_TRIGO_FRACT;
		}

		mFuelLevel -= pDuration * eFuelConsuming[mHoverModel];
	}
	// Determine new dispacement
	Cylinder lShape;

	lShape.mRay = eCharacterMovementRay;	  //eCharacterRay;

	// Compute speed objectives
	MR_3DCoordinate lTranslation((int)(mXSpeed * (int) pDuration), (int)(mYSpeed * (int) pDuration), (int)(mZSpeed * (int) pDuration));

	// This avoid continious jumping
	if(lTranslation.mZ == 0)
		lTranslation.mZ = -1;

	// Verify if the move is valid
	Model::ObstacleCollisionReport lReport;

	BOOL lSuccessfullTry;
	MR_SimulationTime lDuration = pDuration;

	lShape.mPosition.mX = mPosition.mX + lTranslation.mX;
	lShape.mPosition.mY = mPosition.mY + lTranslation.mY;
	lShape.mPosition.mZ = mPosition.mZ + lTranslation.mZ;

	// Whether any attempt in the loop below actually moved the craft.
	// lSuccessfullTry only reflects the *last* attempt, which can fail after an
	// earlier one already succeeded.
	BOOL lMovedAtAll = FALSE;

	for (;;) {
		lSuccessfullTry = FALSE;

		lReport.GetContactWithObstacles(level, &lShape, pRoom, this);
		if(lReport.IsInMaze()) {
			if(!lReport.HaveContact()) {
				mPosition = lShape.mPosition;
				pRoom = lReport.Room();
				mOnFloor = FALSE;

				lSuccessfullTry = TRUE;
			} else {
				// Determine if we can go on the object
				if((lReport.SpaceToCeiling() > 0) && (lReport.StepHeight() <= 1 - lTranslation.mZ)) {
					lShape.mPosition.mZ += lReport.StepHeight();
					lReport.GetContactWithObstacles(level, &lShape, pRoom, this);

					if(lReport.IsInMaze()) {
						if(!lReport.HaveContact()) {
							if((mRenderer != NULL) && (!mOnFloor))
								mInternalSoundList.Add(mRenderer->GetBumpSound());

							mZSpeed = 0;
							mOnFloor = TRUE;

							mPosition = lShape.mPosition;
							pRoom = lReport.Room();

							lSuccessfullTry = TRUE;
						}
					}
				}
				else if((mZSpeed > 0) && (lReport.SpaceToFloor() > 0) && (lReport.CeilingStepHeight() <= lTranslation.mZ)) {
					lShape.mPosition.mZ -= lReport.CeilingStepHeight() + 1;

					lReport.GetContactWithObstacles(level, &lShape, pRoom, this);

					if(lReport.IsInMaze()) {
						if(!lReport.HaveContact())
							mZSpeed = 0;
						mOnFloor = FALSE;

						mPosition = lShape.mPosition;
						pRoom = lReport.Room();

						lSuccessfullTry = TRUE;
					}
				}
			}
		}

		if (lSuccessfullTry) lMovedAtAll = TRUE;

		if(lDuration < MINIMUM_SPLITTABLE_TIME_SLICE)
			break;
		else {
			if(lSuccessfullTry) {
				if(lDuration == pDuration)
					break;					  // success on first attempt

				lDuration /= 2;

				lShape.mPosition.mX += lDuration * lTranslation.mX / pDuration;
				lShape.mPosition.mY += lDuration * lTranslation.mY / pDuration;
				lShape.mPosition.mZ += lDuration * lTranslation.mZ / pDuration;
			} else {
				lDuration /= 2;

				lShape.mPosition.mX -= lDuration * lTranslation.mX / pDuration;
				lShape.mPosition.mY -= lDuration * lTranslation.mY / pDuration;
				lShape.mPosition.mZ -= lDuration * lTranslation.mZ / pDuration;
			}
		}
	}

	// Wall sliding.
	//
	// The loop above only ever scales the *combined* translation vector down,
	// so if every scale of that one direction is blocked the craft stops dead
	// and stays pinned -- at full speed, with the motor still pushing it into
	// the wall. That is the "hit it at the right angle and stick" case.
	//
	// Track walls are arbitrary polygon edges, not axis-aligned, and the
	// collision report does not expose a surface normal, so instead of
	// decomposing onto X/Y we fan the translation out to either side and take
	// the first direction that is actually free. The smallest deviation wins,
	// which is what reads as sliding along the wall.
	if(!lMovedAtAll && (lTranslation.mX != 0 || lTranslation.mY != 0)) {
		// The fan has to reach past 90 degrees. Any rotation short of that
		// still leaves a component pointing into the wall, so for a near
		// head-on hit every direction under 90 degrees is blocked too --
		// which is exactly the case that pins the craft. The last few angles
		// steer slightly away from the surface and free it.
		static const double lSlideAngles[] = {
			0.35, -0.35, 0.70, -0.70, 1.05, -1.05, 1.40, -1.40,
			1.5708, -1.5708, 1.75, -1.75, 1.92, -1.92
		};

		for(double lAngle : lSlideAngles) {
			const double lCos = cos(lAngle);
			const double lSin = sin(lAngle);

			MR_Int32 lSlideX = (MR_Int32)
				(lTranslation.mX * lCos - lTranslation.mY * lSin);
			MR_Int32 lSlideY = (MR_Int32)
				(lTranslation.mX * lSin + lTranslation.mY * lCos);

			// A zero-length candidate would "succeed" without moving the craft
			// anywhere, which would report the stuck state as resolved.
			if(lSlideX == 0 && lSlideY == 0)
				continue;

			lShape.mPosition.mX = mPosition.mX + lSlideX;
			lShape.mPosition.mY = mPosition.mY + lSlideY;
			lShape.mPosition.mZ = mPosition.mZ;

			lReport.GetContactWithObstacles(level, &lShape, pRoom, this);
			if(lReport.IsInMaze() && !lReport.HaveContact()) {
				mPosition = lShape.mPosition;
				pRoom = lReport.Room();
				lMovedAtAll = TRUE;
				break;
			}
		}
	}

	// Last resort: the craft has ended up *inside* the geometry, so its current
	// position reports contact and therefore every candidate move does too --
	// no scaling and no slide direction can ever succeed, and it stays wedged
	// in the wall permanently.
	//
	// Note this cannot be solved by damping the speed: the translation is
	// computed as (int)(speed * duration), so bleeding speed off just makes it
	// truncate to zero, which wedges the craft even harder.
	//
	// Search outward in rings for the nearest free spot and put the craft
	// there, killing the horizontal speed so it settles instead of immediately
	// driving back in.
	if(!lMovedAtAll) {
		lShape.mPosition = mPosition;
		lReport.GetContactWithObstacles(level, &lShape, pRoom, this);

		if(!lReport.IsInMaze() || lReport.HaveContact()) {
			const int lDirections = 16;
			BOOL lFreed = FALSE;

			for(int lRing = 1; (lRing <= 6) && !lFreed; lRing++) {
				const MR_Int32 lDist = (eCharacterRay / 3) * lRing;

				for(int lStep = 0; (lStep < lDirections) && !lFreed; lStep++) {
					const double lDir =
						(2.0 * M_PI * lStep) / (double) lDirections;

					lShape.mPosition.mX =
						mPosition.mX + (MR_Int32)(cos(lDir) * lDist);
					lShape.mPosition.mY =
						mPosition.mY + (MR_Int32)(sin(lDir) * lDist);
					lShape.mPosition.mZ = mPosition.mZ;

					lReport.GetContactWithObstacles(level, &lShape, pRoom, this);
					if(lReport.IsInMaze() && !lReport.HaveContact()) {
						mPosition = lShape.mPosition;
						pRoom = lReport.Room();
						mXSpeed = 0;
						mYSpeed = 0;
						lFreed = TRUE;
					}
				}
			}
		}
	}

	if((mFuelLevel < 0.0) && mMasterMode)
		mMotorOnState = FALSE;

	mRoom = pRoom;
	return pRoom;
}

const Model::ShapeInterface *MainCharacter::GetObstacleShape()
{
	//FIXME: Wait, what?  Is this correct?
	return NULL;
	mCollisionShape.mPosition = mPosition;
	return &mCollisionShape;
}

void MainCharacter::ApplyEffect(const Model::ContactEffect *pEffect,
	MR_SimulationTime pTime, MR_SimulationTime pDuration,
	BOOL pValidDirection, MR_Angle pHorizontalDirection,
	MR_Int32, MR_Int32, Model::Track &track)
{
	using namespace HoverRace::Model;

	auto level = track.GetLevel();

	const PhysicalCollision *lPhysCollision = dynamic_cast<const PhysicalCollision*>(pEffect);
	const SpeedDoubler *lSpeedDoubler = dynamic_cast<const SpeedDoubler*>(pEffect);
	const FuelGain *lFuelGain = dynamic_cast<const FuelGain*>(pEffect);
	const LostOfControl *lLostOfControl = dynamic_cast<const LostOfControl*>(pEffect);
	const CheckPoint *lLapCompleted = dynamic_cast<const CheckPoint*>(pEffect);
	const PowerUpEffect *lPowerUp = dynamic_cast<const PowerUpEffect*>(pEffect);

	if((lPhysCollision != NULL) && pValidDirection) {
		/*
		   double lInvertDampingFactor = 1.0;
		   double lDampingFactor       = 0.0;

		   if( pZMax-210 < mPosition.mZ )
		   {
		   lInvertDampingFactor = (pZMax-mPosition.mZ)/210;
		   lDampingFactor       = 1.0-lInvertDampingFactor;

		   mZSpeed = max( mZSpeed, 512*lDampingFactor*-eZAccell[mHoverModel] );
		   }
		 */

		if(pValidDirection) {
			InertialMoment lMoment;

			lMoment.mWeight = eCharacterWeight[mHoverModel];
			lMoment.mXSpeed = (int)(mXSpeed * 256);
			lMoment.mYSpeed = (int)(mYSpeed * 256);
			lMoment.mZSpeed = 0;

			lMoment.ComputeCollision(lPhysCollision, pHorizontalDirection);

			// mXSpeed = (lInvertDampingFactor*lMoment.mXSpeed/256.0)+lDampingFactor*mXSpeed;
			// mYSpeed = (lInvertDampingFactor*lMoment.mYSpeed/256.0)+lDampingFactor*mYSpeed;
			mXSpeed = lMoment.mXSpeed / 256.0;
			mYSpeed = lMoment.mYSpeed / 256.0;

			if((mRenderer != NULL) && !((lMoment.mXSpeed == 0) && (lMoment.mYSpeed == 0))) {
				mInternalSoundList.Add(mRenderer->GetBumpSound());
				mExternalSoundList.Add(mRenderer->GetBumpSound());
			}

			if((lPhysCollision->mXSpeed != 0 || lPhysCollision->mYSpeed != 0)) {
				mNetPriority = TRUE;
				mLastCollisionTime = pTime;
			}
		}
	}

	if(lSpeedDoubler != NULL) {
		/*
		   mXSpeed *= 2;
		   mYSpeed *= 2;

		   double lAbsSpeed =  sqrt( mXSpeed*mXSpeed+mYSpeed*mYSpeed );

		   if( lAbsSpeed > eSteadySpeed/4 )
		   {
		mXSpeed = eSteadySpeed*2*mXSpeed/lAbsSpeed;
		mYSpeed = eSteadySpeed*2*mYSpeed/lAbsSpeed;
		   }
		 */

		mXSpeed = (4 * eSteadySpeed[0] * MR_Cos[mCabinOrientation]) / MR_TRIGO_FRACT;
		mYSpeed = (4 * eSteadySpeed[0] * MR_Sin[mCabinOrientation]) / MR_TRIGO_FRACT;
	}

	if((lFuelGain != NULL) && mMasterMode) {
		mFuelLevel += pDuration * lFuelGain->mFuelQty;

		if(mFuelLevel > eFuelCapacity) {
			mFuelLevel = eFuelCapacity;
		}
	}

	if((lLostOfControl != NULL) && mMasterMode) {
		if(mOutOfControlDuration < 1750)
			mLastHits.Add(lLostOfControl->mHoverId);

		mOutOfControlDuration = 2000;

		if (mRenderer) {
			mInternalSoundList.Add(mRenderer->GetOutOfCtrlSound());
			mExternalSoundList.Add(mRenderer->GetOutOfCtrlSound());
		}

		if(lLostOfControl->mType == LostOfControl::eMine) {
			mZSpeed = 1.1 * eMaxZSpeed[0];

			if((lLostOfControl->mElementId != -1) && !mMineList.Full() && (mGameOpts & OPT_ALLOW_MINES)) {
				mMineList.Add(lLostOfControl->mElementId);
				level->SetPermElementPos(lLostOfControl->mElementId, -1, mPosition);
			}
		}
	}

	if(((lPowerUp != NULL) && mMasterMode) && (mGameOpts & OPT_ALLOW_CANS)) {
		if((mPowerUpLeft == 0) && (lPowerUp->mElementPermId != -1) && !mPowerUpList.Full()) {
			mPowerUpList.Add(lPowerUp->mElementPermId);
			level->SetPermElementPos(lPowerUp->mElementPermId, -1, mPosition);
			mInternalSoundList.Add(mRenderer->GetPickupSound());
			mExternalSoundList.Add(mRenderer->GetPickupSound());
		}
	}

	if((lLapCompleted != NULL) && mMasterMode && !finished) {
		switch (lLapCompleted->mType) {
			case CheckPoint::eCheck1:
				if (!mCheckPoint1 && !mCheckPoint2) {
					checkpointSignal(this, 1);
					mCheckPoint1 = TRUE;
					mCheckPoint2 = FALSE;
				}
				break;
			case CheckPoint::eCheck2:
				if (mCheckPoint1)
					if (!mCheckPoint2) {
						checkpointSignal(this, 2);
						mCheckPoint2 = TRUE;
					}
				break;
			case CheckPoint::eFinishLine:
				if(mCheckPoint2) {
					mCheckPoint1 = FALSE;
					mCheckPoint2 = FALSE;

					// The finish line is the first checkpoint, but we fire
					// a separate signal for convenience.
					checkpointSignal(this, 0);
					finishLineSignal(this);

					mLastLapDuration = pTime - mLastLapCompletion;
					mLastLapCompletion = pTime;

					if (mRenderer) {
						if (finished) {
							mInternalSoundList.Add(mRenderer->GetFinishSound());
							mExternalSoundList.Add(mRenderer->GetFinishSound());
						} else {
							mInternalSoundList.Add(mRenderer->GetLineCrossingSound());
							mExternalSoundList.Add(mRenderer->GetLineCrossingSound());
						}
					}
				}
				break;
		}
	}
}

const Model::ContactEffectList *MainCharacter::GetEffectList()
{
	mContactEffect.mWeight = eCharacterWeight[mHoverModel];
	mContactEffect.mXSpeed = (int)(mXSpeed * 256);
	mContactEffect.mYSpeed = (int)(mYSpeed * 256);
	mContactEffect.mZSpeed = 0;

	return &mContactEffectList;
}

const Model::ShapeInterface *MainCharacter::GetReceivingContactEffectShape()
{
	mCollisionShape.mPosition = mPosition;
	return &mCollisionShape;
}

const Model::ShapeInterface *MainCharacter::GetGivingContactEffectShape()
{
	mContactShape.mPosition = mPosition;
	return &mContactShape;
}

// MainCharacter::Cylinder
MR_Int32 MainCharacter::Cylinder::ZMin() const
{
	return mPosition.mZ;
}

MR_Int32 MainCharacter::Cylinder::ZMax() const
{
	return mPosition.mZ + eCharacterHeight;
}

MR_Int32 MainCharacter::Cylinder::AxisX() const
{
	return mPosition.mX;
}

MR_Int32 MainCharacter::Cylinder::AxisY() const
{
	return mPosition.mY;
}

MR_Int32 MainCharacter::Cylinder::RayLen() const
{
	return mRay;
}

// Done with the cylinder stuff
MR_Angle MainCharacter::GetCabinOrientation() const
{
	return mCabinOrientation;
}

/**
 * Retrieve the relative amount of fuel remaining.
 * @return The fuel level, where 1.0 or higher is full and
 *         0.0 or lower is empty.
 */
double MainCharacter::GetFuelLevel() const
{
	return mFuelLevel / eFuelCapacity;
}

MainCharacter::eWeapon MainCharacter::GetCurrentWeapon() const
{
	return mCurrentWeapon;
}

int MainCharacter::GetMissileRefillLevel(int pNbLevel) const
{
	if(mGameOpts & OPT_ALLOW_WEAPONS)
		return (pNbLevel - 1) * (eMissileRefillTime - mMissileRefillDuration) / eMissileRefillTime;
	else
		return 0;
}

int MainCharacter::GetMineCount() const
{
	return mMineList.Used();
}

int MainCharacter::GetPowerUpCount() const
{
	return mPowerUpList.Used();
}

int MainCharacter::GetPowerUpFraction(int pNbLevel) const
{
	int lReturnValue = 0;
	if(mPowerUpLeft > 0) {
		lReturnValue = 1 + ((mPowerUpLeft - 1) * pNbLevel / ePwrUpDuration);
		if(lReturnValue > pNbLevel)
			lReturnValue = pNbLevel;
	}
	return lReturnValue;
}

double MainCharacter::GetAbsoluteSpeed() const
{
	double lReturnValue = sqrt(mXSpeed * mXSpeed + mYSpeed * mYSpeed) / (eSteadySpeed[0] * 1.9);
	if(lReturnValue > 1.0)
		lReturnValue = 1.0;
	return lReturnValue;
}

double MainCharacter::GetDirectionalSpeed() const
{
	double lReturnValue = (mXSpeed * MR_Cos[mCabinOrientation] + mYSpeed * MR_Sin[mCabinOrientation]) / (MR_TRIGO_FRACT * eSteadySpeed[0] * 1.9);

	if(lReturnValue > 1.0)
		lReturnValue = 1.0;
	else if(lReturnValue < -1.0)
		lReturnValue = -1.0;

	return lReturnValue;
}

MR_SimulationTime MainCharacter::GetTotalTime() const
{
	return mLastLapCompletion;
}

MR_SimulationTime MainCharacter::GetLastLapDuration() const
{
	return mLastLapDuration;
}

MR_SimulationTime MainCharacter::GetLastLapCompletion() const
{
	return mLastLapCompletion;
}

/**
 * Determine if the player has started playing (pregame is over and the player
 * has control of the craft).
 * @return @c true if the play session has started,
 *         @c false if still in pregame.
 */
bool MainCharacter::HasStarted() const
{
	return started;
}

/**
 * Signal that the player's session has ended.
 */
void MainCharacter::Finish()
{
	if (!finished) {
		finished = true;
		finishedSignal(this);
	}
}

/**
 * Determine if the player's session has ended.
 * @return @c true if the session is over,
 *         @c false if the player is still playing.
 */
bool MainCharacter::HasFinish() const
{
	return finished;
}

int MainCharacter::HitQueueCount() const
{
	return mLastHits.Used();
}

int MainCharacter::GetHitQueue()
{
	int lReturnValue = mLastHits.GetHead();
	mLastHits.Remove();
	return lReturnValue;
}

void MainCharacter::PlayInternalSounds()
{
	if (mRenderer) {
		// Sound events
		while(!mInternalSoundList.IsEmpty()) {
			SoundServer::Play(mInternalSoundList.GetHead());
			mInternalSoundList.Remove();
		}

		// Continuous sounds
		ContinuousSound *lWindSound = mRenderer->GetFrictionSound();
		ContinuousSound *lMotorSound = mRenderer->GetMotorSound();
		double lAbsSpeed = sqrt(mXSpeed * mXSpeed + mYSpeed * mYSpeed) / (eSteadySpeed[0]);

		if(lAbsSpeed > 0.02)
			SoundServer::Play(lWindSound, 0, 0, 1.5 * lAbsSpeed);
		if(mMotorOnState)
			SoundServer::Play(lMotorSound, 0);
	}
}

void MainCharacter::PlayExternalSounds(int pDB, int pPan)
{
	if (mRenderer) {
		// Sound events
		while(!mExternalSoundList.IsEmpty()) {
			SoundServer::Play(mExternalSoundList.GetHead(), pDB, 1.0, pPan);
			mExternalSoundList.Remove();
		}

		// Continuous sounds
		ContinuousSound *lWindSound = mRenderer->GetFrictionSound();
		ContinuousSound *lMotorSound = mRenderer->GetMotorSound();
		double lAbsSpeed = sqrt(mXSpeed * mXSpeed + mYSpeed * mYSpeed) / (eSteadySpeed[0]);

		if(lAbsSpeed > 0.02)
			SoundServer::Play(lWindSound, 1, pDB, 1.5 * lAbsSpeed, pPan);
		if(mMotorOnState)
			SoundServer::Play(lMotorSound, 1, pDB, 1.0, pPan);
	}
}

}  // namespace MainCharacter
}  // namespace HoverRace
