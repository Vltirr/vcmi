/*
 * CLoadFile.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CLoadFile.h"

#include "../texts/TextOperations.h"

template <typename From, typename To>
struct static_caster
{
	To operator()(From p) {return static_cast<To>(p);}
};


CLoadFile::CLoadFile(const boost::filesystem::path & fname, IGameInfoCallback * cb)
	: serializer(this)
	, sfile(fname.c_str(), std::ios::in | std::ios::binary)
{
	serializer.cb = cb;
	serializer.loadingGamestate = true;
	assert(!serializer.reverseEndianness);

	sfile.exceptions(std::ifstream::failbit | std::ifstream::badbit); //we throw a lot anyway

	if(!sfile)
		throw std::runtime_error("Error: cannot open file '" + TextOperations::filesystemPathToUtf8(fname) + "' for reading!");

	static const std::string MAGIC = "VCMI";
	std::string readMagic = MAGIC;
	sfile.read(readMagic.data(), 4);
	if(readMagic != MAGIC)
		throw std::runtime_error("Error: '" + TextOperations::filesystemPathToUtf8(fname) + "' is not a VCMI file!");

	sfile.read(reinterpret_cast<char*>(&serializer.version), sizeof(serializer.version));

	if(!isSupportedSerializationVersion(serializer.version))
	{
		auto * versionptr = reinterpret_cast<char *>(&serializer.version);
		std::reverse(versionptr, versionptr + sizeof(serializer.version));
		if(!isSupportedSerializationVersion(serializer.version))
			throw std::runtime_error("Unsupported save format in '" + TextOperations::filesystemPathToUtf8(fname) + "'!");
		serializer.reverseEndianness = true;
	}

	std::string loaded = SAVEGAME_MAGIC;
	sfile.read(loaded.data(), SAVEGAME_MAGIC.length());
	if(loaded != SAVEGAME_MAGIC)
		throw std::runtime_error("Magic bytes doesn't match!");
}

int CLoadFile::read(std::byte * data, unsigned size)
{
	sfile.read(reinterpret_cast<char *>(data), size);
	return size;
}
