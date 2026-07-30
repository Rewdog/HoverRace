
// Chronometer.h
//
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

#pragma once

#include "../Util/Duration.h"

#include "HudDecor.h"

#if defined(_WIN32) && defined(HR_ENGINE_SHARED)
#	ifdef MR_ENGINE
#		define MR_DllDeclare   __declspec( dllexport )
#	else
#		define MR_DllDeclare   __declspec( dllimport )
#	endif
#else
#	define MR_DllDeclare
#endif

namespace HoverRace {
	namespace Display {
		class ActiveText;
		class Label;
	}
	namespace Util {
		class Clock;
	}
}

namespace HoverRace {
namespace Display {

/**
 * Display the current game time.
 * @author Michael Imamura
 */
class MR_DllDeclare Chronometer : public HudDecor
{
	using SUPER = HudDecor;

public:
	Chronometer(Display &display, const std::string &title,
		std::shared_ptr<Util::Clock> clock);
	virtual ~Chronometer() { }

protected:
	void OnHudRescaled(const Vec2 &hudScale) override;

protected:
	void Layout() override;
public:
	void Advance(Util::OS::timestamp_t tick) override;

	/**
	 * Restart the displayed time from now.
	 *
	 * The clock itself is shared (usually the session clock, which something
	 * else is responsible for advancing), so this records an origin and
	 * displays the time elapsed since it rather than touching the clock.
	 * That is what lets a lap timer and a total-race timer run off one clock.
	 */
	void Reset();

private:
	std::shared_ptr<Util::Clock> clock;

	/// Displayed time is measured from here once hasOrigin is set. A flag is
	/// used rather than testing origin against zero, since a lap can
	/// legitimately begin at time zero.
	Util::Duration origin;
	bool hasOrigin = false;

	Util::OS::timestamp_t lastTick;

	std::shared_ptr<FillBox> bg;
	std::shared_ptr<Label> titleLbl;
	std::shared_ptr<ActiveText> valueLbl;
};

}  // namespace Display
}  // namespace HoverRace

#undef MR_DllDeclare
