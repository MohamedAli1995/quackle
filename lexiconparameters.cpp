/*
 *  Quackle -- Crossword game artificial intelligence and analysis tool
 *  Copyright (C) 2005-2014 Jason Katz-Brown and John O'Laughlin.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <fstream>


#include "datamanager.h"
#include "lexiconparameters.h"
#include "uv.h"

using namespace Quackle;

class Quackle::V0LexiconInterpreter : public LexiconInterpreter
{

	virtual void loadDawg(ifstream &file, LexiconParameters &lexparams)
	{
		int i = 0;
		while (!file.eof())
		{
			file.read((char*)(lexparams.m_dawg) + i, 7);
			i += 7;
		}
	}

	virtual void loadGaddag(ifstream &file, LexiconParameters &lexparams)
	{
		int i = 0;
		while (!file.eof())
		{
			file.read((char*)(lexparams.m_gaddag) + i, 4);
			i += 4;
		}
	}

	virtual void dawgAt(const unsigned char *dawg, int index, unsigned int &p, Letter &letter, bool &t, bool &lastchild, bool &british, int &playability) const
	{
		index *= 7;
		p = (dawg[index] << 16) + (dawg[index + 1] << 8) + (dawg[index + 2]);
		letter = dawg[index + 3];
		
		t = (letter & 32) != 0;
		lastchild = (letter & 64) != 0;
		british = !(letter & 128);
		letter = (letter & 31) + QUACKLE_FIRST_LETTER;

		playability = (dawg[index + 4] << 16) + (dawg[index + 5] << 8) + (dawg[index + 6]);
	}
	virtual int versionNumber() const { return 0; }
};

class Quackle::V1LexiconInterpreter : public LexiconInterpreter
{

	virtual void loadDawg(ifstream &file, LexiconParameters &lexparams)
	{
		int i = 0;
		unsigned char bytes[3];
		file.get(); // skip past version byte
		file.read(lexparams.m_hash, sizeof(lexparams.m_hash));
		file.read((char*)bytes, 3);
		lexparams.m_wordcount = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
		while (!file.eof())
		{
			file.read((char*)(lexparams.m_dawg) + i, 7);
			i += 7;
		}
	}

	virtual void loadGaddag(ifstream &file, LexiconParameters &lexparams)
	{
		char hash[16];
		file.get(); // skip past version byte
		file.read(hash, sizeof(hash));
		if (memcmp(hash, lexparams.m_hash, sizeof(hash)))
		{
			// If we're using a v0 DAWG, then ignore the hash
			for (size_t i = 0; i < sizeof(lexparams.m_hash); i++)
			{
				if (lexparams.m_hash[0] != 0)
				{
					lexparams.unloadGaddag(); // don't use a mismatched gaddag
					return;
				}
			}
		}

		size_t i = 0;
		while (!file.eof())
		{
			file.read((char*)(lexparams.m_gaddag) + i, 4);
			i += 4;
		}
	}

	virtual void dawgAt(const unsigned char *dawg, int index, unsigned int &p, Letter &letter, bool &t, bool &lastchild, bool &british, int &playability) const
	{
		index *= 7;
		p = (dawg[index] << 16) + (dawg[index + 1] << 8) + (dawg[index + 2]);
		letter = dawg[index + 3];
		
		t = (p != 0);
		lastchild = ((letter & 64) != 0);
		british = !(letter & 128);
		letter = (letter & 63) + QUACKLE_FIRST_LETTER;

		playability = (dawg[index + 4] << 16) + (dawg[index + 5] << 8) + (dawg[index + 6]);
	}
	virtual int versionNumber() const { return 1; }
};

LexiconParameters::LexiconParameters()
	: m_dawg(NULL), m_gaddag(NULL), m_interpreter(NULL), m_wordcount(0)	
{
	memset(m_hash, 0, sizeof(m_hash));
}

LexiconParameters::~LexiconParameters()
{
	unloadAll();
}

void LexiconParameters::unloadAll()
{
	unloadDawg();
	unloadGaddag();
}

void LexiconParameters::unloadDawg()
{
	delete[] m_dawg;
	m_dawg = NULL;
	delete m_interpreter;
}

void LexiconParameters::unloadGaddag()
{
	delete[] m_gaddag;
	m_gaddag = NULL;
}

void LexiconParameters::loadDawg(const string &filename)
{
	unloadDawg();

	ifstream file(filename.c_str(), ios::in | ios::binary);
	if (!file.is_open())
	{
		UVcout << "couldn't open dawg " << filename.c_str() << endl;
		return;
	}

	char versionByte = file.get();
	m_interpreter = createInterpreter(versionByte);
	if (m_interpreter == NULL)
	{
		UVcout << "couldn't open file " << filename.c_str() << endl;
		return;
	}

	file.seekg(0, ios_base::end);
	m_dawg = new unsigned char[file.tellg()];
	file.seekg(0, ios_base::beg);

	m_interpreter->loadDawg(file, *this);
}

void LexiconParameters::loadGaddag(const string &filename)
{
	unloadGaddag();

	ifstream file(filename.c_str(), ios::in | ios::binary);
	if (!file.is_open())
	{
		UVcout << "couldn't open gaddag " << filename.c_str() << endl;
		UVcout << "Performance without gaddag won't be quite so awesome." << endl;
		return;
	}

	char versionByte = file.get();
	if (versionByte < m_interpreter->versionNumber())
		return;
	file.seekg(0, ios_base::end);
	m_gaddag = new unsigned char[file.tellg()];
	file.seekg(0, ios_base::beg);

	// must create a local interpreter because dawg/gaddag versions might not match
	LexiconInterpreter* interpreter = createInterpreter(versionByte);
	if (interpreter != NULL)
	{
		interpreter->loadGaddag(file, *this);
		delete interpreter;
	}
	else
		unloadGaddag();
}

string LexiconParameters::findDictionaryFile(const string &lexicon)
{
	return QUACKLE_DATAMANAGER->findDataFile("lexica", lexicon);
}

UVString LexiconParameters::hashString(bool shortened) const
{
	const char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	string hashStr;
	for (size_t i = 0; i < sizeof(m_hash); i++)
	{
		hashStr.push_back(hex[(m_hash[i] & 0xF0) >> 4]);
		hashStr.push_back(hex[m_hash[i] & 0x0F]);
		if (shortened && i == 5)
			break;
		if (i % 2 == 1)
			hashStr.push_back('-');
	}
	return hashStr;
}

LexiconInterpreter* LexiconParameters::createInterpreter(char version) const
{
	switch(version)
	{
		case 0:
			return new V0LexiconInterpreter();
		case 1:
			return new V1LexiconInterpreter();
		default:
			return NULL;
	}
}
