
// TrackDump/main.cpp
//
// Dumps HoverRace track geometry to JSON.
//
// The .trk format is the original 1997 GrokkSoft binary record format, so
// rather than reimplement a parser, this links against the real engine and
// walks Model::Level with the same accessors the game renderer uses.
//
// Usage:
//   trackdump <track name> [output.json]
//
// Track names are the bundle names, e.g. "ClassicH", not file paths.

// The engine headers rely on the project prelude for BOOL, HR_UNUSED, etc.
#include "../StdAfx.h"

#include <hoverrace/hr-version.h>

#include <iostream>
#include <fstream>
#include <iomanip>

#include "../../engine/Model/Level.h"
#include "../../engine/Model/Track.h"
#include "../../engine/Model/GameOptions.h"
#include "../../engine/Parcel/TrackBundle.h"
#include "../../engine/Util/Config.h"
#include "../../engine/Util/Log.h"

using namespace HoverRace;
using HoverRace::Util::Config;

namespace {

/// Emit one polygon ring (a room or a feature) as JSON.
void DumpRing(std::ostream &os, const Model::Level &level, int roomId,
	bool feature, const char *indent)
{
	const int count = feature ?
		level.GetFeatureVertexCount(roomId) :
		level.GetRoomVertexCount(roomId);

	const MR_Int32 bottom = feature ?
		level.GetFeatureBottomLevel(roomId) :
		level.GetRoomBottomLevel(roomId);
	const MR_Int32 top = feature ?
		level.GetFeatureTopLevel(roomId) :
		level.GetRoomTopLevel(roomId);

	os << indent << "{\n";
	os << indent << "  \"bottom\": " << bottom << ",\n";
	os << indent << "  \"top\": " << top << ",\n";
	os << indent << "  \"vertices\": [";
	for (int i = 0; i < count; i++) {
		const auto &v = feature ?
			level.GetFeatureVertex(roomId, i) :
			level.GetRoomVertex(roomId, i);
		if (i) os << ", ";
		os << "[" << v.mX << "," << v.mY << "]";
	}
	os << "],\n";

	// A wall with no neighbour is solid; walls that open onto another room
	// are portals and must not be rendered as geometry.
	os << indent << "  \"solid\": [";
	for (int i = 0; i < count; i++) {
		if (i) os << ", ";
		if (feature) {
			os << "true";
		}
		else {
			os << (level.GetNeighbor(roomId, i) < 0 ? "true" : "false");
		}
	}
	os << "]\n";
	os << indent << "}";
}

}  // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::cerr << "Usage: trackdump <track name> [output.json]\n";
		return 1;
	}

	const std::string trackName = argv[1];
	const std::string outPath = (argc > 2) ? argv[2] : "";

	Util::Log::Init(false);

	// Media path defaults to the compiled-in share/ location.
	auto &cfg = Config::Init(PACKAGE, HR_APP_VERSION,
		HR_APP_VERSION_PRERELEASE, Util::OS::path_t(), Util::OS::path_t());
	cfg.Load();

	std::shared_ptr<Model::Track> track;
	try {
		track = cfg.GetTrackBundle().OpenTrack(trackName);
	}
	catch (Exception &ex) {
		std::cerr << "Failed to open track: " << ex.what() << "\n";
		return 1;
	}
	if (!track) {
		std::cerr << "Track not found: " << trackName << "\n";
		return 1;
	}

	Model::GameOptions opts;
	track->Load(false, opts);

	const Model::Level *level = track->GetLevel();
	if (!level) {
		std::cerr << "Track has no level data.\n";
		return 1;
	}

	std::ofstream fileOut;
	if (!outPath.empty()) {
		fileOut.open(outPath);
		if (!fileOut) {
			std::cerr << "Cannot write: " << outPath << "\n";
			return 1;
		}
	}
	std::ostream &os = outPath.empty() ? std::cout : fileOut;

	const int roomCount = level->GetRoomCount();
	const int playerCount = level->GetPlayerCount();

	os << std::fixed << std::setprecision(0);
	os << "{\n";
	os << "  \"name\": \"" << trackName << "\",\n";
	os << "  \"roomCount\": " << roomCount << ",\n";

	// Starting positions, so the web build can put the craft where the
	// original game does.
	os << "  \"starts\": [";
	for (int i = 0; i < playerCount; i++) {
		if (i) os << ", ";
		os << level->GetStartingRoom(i);
	}
	os << "],\n";

	os << "  \"rooms\": [\n";
	for (int r = 0; r < roomCount; r++) {
		if (r) os << ",\n";
		DumpRing(os, *level, r, false, "    ");
	}
	os << "\n  ],\n";

	// Features are the raised/lowered obstacles inside rooms.
	os << "  \"features\": [\n";
	bool firstFeature = true;
	for (int r = 0; r < roomCount; r++) {
		const int featureCount = level->GetFeatureCount(r);
		for (int f = 0; f < featureCount; f++) {
			if (!firstFeature) os << ",\n";
			firstFeature = false;
			DumpRing(os, *level, level->GetFeature(r, f), true, "    ");
		}
	}
	os << "\n  ]\n";
	os << "}\n";

	std::cerr << "Dumped " << roomCount << " rooms from " << trackName << "\n";

	return 0;
}
