#include <algorithm>
#include <cstdio>
#include <inttypes.h>
#include <cxxmidi/converters.hpp>
#include <cxxmidi/file.hpp>
#include <cxxmidi/note.hpp>

typedef struct {
	uint8_t sID;      /* SEvent type code                             */
	uint8_t data;     /* sID-dependent data                           */
} SEvent;


typedef struct{
	uint32_t time;
	uint8_t data;//pitxh or program number
	uint8_t velocity;
	bool tied;
	bool progChange;
} MidiEvent;

bool MidiEventCompare(const MidiEvent& i,const MidiEvent& j) { return (i.time < j.time); }



void parseSEvent(unsigned char* buffer, uint32_t offset,uint32_t dt, uint32_t & time,uint8_t &velocity,std::vector<MidiEvent> &events)
{
	unsigned char sID = buffer[offset];
	if ((0 <= sID) && (128 >= sID))
	{
		//Note event
		bool chord = (buffer[offset + 1] & 0x80);
		bool tieOut = (buffer[offset + 1] & 0x40);
		uint8_t nTuplet = ((buffer[offset + 1] & 0x30) >> 4);
		bool dot = (buffer[offset + 1] & 0x08);
		uint8_t division = (buffer[offset + 1] & 0x07);

		uint32_t duration = dt * (powf(2.0f, 2 - division)) * (dot ? 1.5f : 1.0f);
		if (nTuplet > 0)
			duration *= (2*nTuplet) / (2*nTuplet+1.0f);
		uint32_t nextStart = chord ? 0 : duration;

		if (sID < 128)
		{
			//Note
			MidiEvent noteOn;
			noteOn.time = time;
			noteOn.data = sID;
			noteOn.velocity = velocity;
			noteOn.tied = false;
			noteOn.progChange = false;
			events.push_back(noteOn);

			MidiEvent noteOff;
			noteOff.time = time + duration;
			noteOff.data = sID;
			noteOff.velocity = 0;//Note Off
			noteOff.tied = tieOut;
			noteOff.progChange = false;
			events.push_back(noteOff);
		}
		else
		{//rest
		}

		time += nextStart;
	}
	else
		switch (sID)
		{
		case 129:
			//set instrument number
		case 134:
			//set midi preset
			//send midi programe change with buffer[offset+1]
			MidiEvent progChange;
			progChange.time = time;
			progChange.data = buffer[offset + 1];
			progChange.velocity = 0;
			progChange.tied = false;
			progChange.progChange = true;
			events.push_back(progChange);

			break;
		case 132:
			//set volume
			velocity = buffer[offset + 1];
			break;
		default :
			break;
		}
}

uint32_t computeLength(unsigned char* buffer, uint32_t offset)
{
	uint32_t length = (buffer[offset] << 24) + (buffer[offset+1] << 16) + (buffer[offset+2] << 8) + (buffer[offset+3] + 8);
	return length;
}

void parseNextChunk(char* buffer, uint32_t& offset,uint32_t dt,uint8_t velocity, CxxMidi::File &myFile)
{
	std::string str;
	str += buffer[offset];
	str += buffer[offset + 1];
	str += buffer[offset + 2];
	str += buffer[offset + 3];
	
	uint32_t length = computeLength((unsigned char *)buffer,offset+4);

	if (str.compare("TRAK") == 0)
	{
		std::cout << "Found track with length = " << length << std::endl;

		CxxMidi::Track& track = myFile.addTrack();

		uint32_t time = 0;

		std::vector<MidiEvent> events;

		for (uint32_t pSEvent = offset + 8; pSEvent < offset + length; pSEvent += 2)
		{
			parseSEvent((unsigned char*)buffer, pSEvent, dt,time,velocity,events);
		}

		std::stable_sort(events.begin(), events.end(), MidiEventCompare);

		time = 0;

		for (std::vector<MidiEvent>::iterator it = events.begin(); it!=events.end(); ++it)
		{
			MidiEvent& event = *it;
			if (event.progChange)
			{
				track.push_back(CxxMidi::Event(0, // deltatime
					CxxMidi::Message::ProgramChange, // message type
					event.data, // program number
					0)); // no data

			}
			else
			{
				if (event.velocity == 0)
				{
					//Note off
					if (event.tied)
					{
						bool found = false;
						for (std::vector<MidiEvent>::iterator it2 = it + 1; it2 != events.end(); ++it2)
						{
							MidiEvent& event2 = *it2;
							if ((event2.velocity > 0) && (event2.data == event.data) && (event2.time == event.time))
								{
									found = true;
									events.erase(it2);
									break;
								}
						}

						if (!found)
						{
							std::cerr << "Cannnot find tied event. Ignoring tie." << std::endl;
							//Forcing Note off
							track.push_back(CxxMidi::Event(event.time-time, // deltatime
								CxxMidi::Message::NoteOn, // message type
								event.data, // pitch
								0)); // velocity = 0 => NoteOff
						}
													
					}
					else
					{
						//Simple note off
						track.push_back(CxxMidi::Event(event.time - time, // deltatime
							CxxMidi::Message::NoteOn, // message type
							event.data, // pitch
							0)); // velocity = 0 => NoteOff
					}
				}
				else
				{
					//NoteOn
					track.push_back(CxxMidi::Event(event.time - time, // deltatime
						CxxMidi::Message::NoteOn, // message type
						event.data, // pitch
						event.velocity)); // velocity = 0 => NoteOff
				}


				time = event.time;
			}
		}
		track.push_back(CxxMidi::Event(0, // deltatime
			CxxMidi::Message::Meta,
			CxxMidi::Message::EndOfTrack));

	}
	else if (str.compare("INS1"))
	{
		//Instrument
		std::string name;
		for (uint32_t i = 12; i < length; ++i)
			name += buffer[offset + i];
		std::cout << "Found instrument with length = " << length << " and name = '" << name << "' -(for your information only , not using this chunk)." << std::endl;
	}
	else 
	{
		//Name , (c) , AUTH , ANNO
		std::string text;
		for (uint32_t i = 9; i < length; ++i)
			text += buffer[offset + i];
		std::cout << "Found " << str << " chunk with length = " << length << " and text = " << text <<
			" -(for your information only , not using this chunk)." << std::endl;
	}

	offset += length;
	if (offset % 2) ++offset; //2-byte data alignment
}


int main(int argc,char ** argv)
{
	uint32_t dt; // quartenote deltatime [ticks]
	// What value should dt be, if we want quarter notes to last 0.5s?

	// Default MIDI time division is 500ticks/quarternote.
	// Default MIDI tempo is 500000us per quarternote
	dt = CxxMidi::Converters::us2dt(500000, // 0.5s
		500000, // tempo [us/quarternote]
		500); // time division [us/quarternote]

	for (int i = 1; i < argc; ++i)
	{

		CxxMidi::File myFile;

		std::cout << "Opening " << argv[i] << "...\n";

		std::ifstream smusFile(argv[i], std::ifstream::binary);

		if (smusFile) {
			// get length of file:
			smusFile.seekg(0, smusFile.end);
			int length = smusFile.tellg();
			smusFile.seekg(0, smusFile.beg);

			char* buffer = new char[length];

			std::cout << "Reading " << length << " characters... ";
			// read data as a block:
			smusFile.read(buffer, length);

			if (smusFile)
				std::cout << "all characters read successfully." << std::endl;
			else
				std::cout << "error: only " << smusFile.gcount() << " could be read" << std::endl;
			smusFile.close();

			// ...buffer contains the entire file...

			std::string form;
			form += buffer[0];
			form += buffer[0 + 1];
			form += buffer[0 + 2];
			form += buffer[0 + 3];
			if (form.compare("FORM"))
			{
				std::cerr << "Cant find FORM chunk. Exiting." << std::endl;
				return -1;
			}
			else
			{
				std::cout << "FORM chunk found. File is effectively an IFF file." << std::endl;
				uint32_t formLength = computeLength((unsigned char*)buffer, 4);
				std::cout << "FORM chunk length = " << formLength << std::endl;
				//Looking for SMUS tag
				std::string smus;
				smus += buffer[8];
				smus += buffer[9];
				smus += buffer[10];
				smus += buffer[11];
				if (smus.compare("SMUS"))
				{
					std::cerr << "Cant find SMUS. Exiting." << std::endl;
					return -1;
				}
				else
				{
					std::cout << "SMUS found. File is effectively a SMUS file." << std::endl;
					//Looking for SHDR tag
					std::string shdr;
					shdr += buffer[12];
					shdr += buffer[13];
					shdr += buffer[14];
					shdr += buffer[15];
					if (shdr.compare("SHDR"))
					{
						std::cerr << "Cant find SHDR. Exiting." << std::endl;
						return -1;
					}
					else
					{
						//Parsing SHDR
						std::cout << "SHDR chunk found." << std::endl;
						uint32_t shdrLength = computeLength((unsigned char*)buffer, 16);
						std::cout << "SHDR chunk length = " << shdrLength << std::endl;
						unsigned char* unsignedBuffer = (unsigned char*)buffer;
						uint16_t tempo = (unsignedBuffer[20] << 8) + unsignedBuffer[21];
						std::cout << "Found Tempo = " << tempo << ". Ignoring , tempo is set to 120bpm by default." << std::endl;
						uint8_t velocity = unsignedBuffer[22];
						std::cout << "Found Volume = " << (int)velocity << std::endl;
						uint8_t tracks = unsignedBuffer[23];
						std::cout << "Found track number = " << (int)tracks << std::endl;
						std::cout << "End of fixed headers. Now Parsing variable chunks." << std::endl;

						//Parsing variable chunks
						uint32_t offset = 24;

						while (offset < formLength)
							parseNextChunk(buffer, offset, dt, velocity, myFile);
					}
				}
			}

			myFile.saveAs((std::string(argv[i])+".mid").c_str());

			delete[] buffer;
		}
		else
			std::cerr << "Cant open file " << argv[i] << std::endl;
	}
	return 0;
}