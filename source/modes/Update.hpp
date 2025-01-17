/***************************************************************************
*  Part of kASA: https://github.com/SilvioWeging/kASA
*
*  Copyright (C) 2020 Silvio Weging <silvio.weging@gmail.com>
*
*  Distributed under the Boost Software License, Version 1.0.
*  (See accompanying file LICENSE_1_0.txt or copy at
*  http://www.boost.org/LICENSE_1_0.txt)
**************************************************************************/
#pragma once

#include "Read.hpp"
#include "Build.hpp"

namespace kASA {
	template<class vecType, class elemType, class intType>
	class Update : public Read<vecType, elemType, intType>, Build<vecType, elemType> {

		typedef Read<vecType, elemType, intType> Base;

	public:
		Update(const string& tmpPath, const int32_t& iNumOfProcs, const int32_t& iHigherK, const int32_t& iLowerK, const int32_t& iNumOfCall, const bool& bVerbose = false, const bool& bTranslated = false, const string& stxxl_mode = "", const bool& bSixFrames = false) : Read<vecType, elemType, intType>(tmpPath, iNumOfProcs, iHigherK, iLowerK, iNumOfCall, bVerbose, bTranslated, stxxl_mode, bSixFrames), Build<vecType, elemType>() {}
		Update(const kASA& obj, const bool& bTranslated = false, const bool& bUnfunny = false) : Read<vecType, elemType, intType>(obj, bTranslated, bUnfunny), Build<vecType, elemType>() {}

	public:


		void DeleteFromLib(const string& contentFile, const string& sLibFile, const string& fOutfile, const string& sDelNodesPath, const bool& bOverwrite) {
			try {
				// test if files exists
				if (!ifstream(sLibFile)) {
					throw runtime_error("Library file does not exist");
				}

				if (!ifstream(sDelNodesPath)) {
					throw runtime_error("delnodes.dmp not found");
				}

				ifstream deletedNodes(sDelNodesPath);
				string line = "";
				unordered_set<uint32_t> vTaxIdsTBD;
				while (getline(deletedNodes, line)) {
					if (line != "") {
						vTaxIdsTBD.insert(stoul((Utilities::split(line, '\t'))[0]));
					}
				}

				// read content file
				uint32_t iIdxCounter = 1;
				unordered_map<uint32_t, string> mIdxToName; mIdxToName[0] = "non_unique";
				unordered_map<uint32_t, uint32_t> mIDsAsIdx; mIDsAsIdx[0] = 0;
				ifstream content(contentFile);
				string sDummy = "";
				while (getline(content, sDummy)) {
					if (sDummy != "") {
						const auto& cline = Utilities::split(sDummy, '\t');
						if (cline.size() >= 4) {
							mIDsAsIdx[stoul(cline[1])] = iIdxCounter;
							mIdxToName[iIdxCounter] = cline[0];
							++iIdxCounter;
						}
						else {
							throw runtime_error("Content file contains less than 4 columns, it may be damaged...");
						}
					}
				}

				unique_ptr<uint64_t[]> arrFrequencies_l(new uint64_t[iIdxCounter * Base::_iHighestK]);
				for (uint64_t i = 0; i < iIdxCounter * Base::_iHighestK; ++i) {
					arrFrequencies_l[i] = 0;
				}

				// Create new vector
				string sTempFile = Base::_sTemporaryPath + "_update_temp_" + to_string(Base::_iNumOfCall);
				{ // need a scope so that the stxxl releases the vector before copying in the case of overwrite == true
					ifstream infoFile(sLibFile + "_info.txt");
					uint64_t iLibSize;
					infoFile >> iLibSize;
					infoFile.close();
					stxxlFile libFile(sLibFile, stxxl::file::RDONLY);
					const vecType vLib(&libFile, iLibSize);
					ofstream dummy;
					//dummy.exceptions(std::ifstream::failbit | std::ifstream::badbit); 
					dummy.open((bOverwrite) ? sTempFile : fOutfile);
					dummy.close();
					stxxlFile outFile((bOverwrite) ? sTempFile : fOutfile, stxxl::file::RDWR);
					vecType vOut(&outFile, iLibSize);

					// All entries with valid taxonomic ids will be written to the other library
					auto itOut = vOut.begin();
					for (auto itLib = vLib.cbegin(); itLib != vLib.cend(); ++itLib) {
						if (vTaxIdsTBD.find(itLib->second) == vTaxIdsTBD.end()) {
							*itOut++ = *itLib;
						}
					}
					vOut.resize(itOut - vOut.begin(), true);
					vOut.export_files("_");

					ofstream infoFileOut(fOutfile + "_info.txt");
					infoFileOut << vOut.size();

					Trie<intType> T(static_cast<int8_t>(((is_same<vecType, contentVecType_128>::value) ? HIGHESTPOSSIBLEK : 12)), static_cast<int8_t>(Base::_iMinK), 6);
					T.SaveToStxxlVec(&vOut, fOutfile, &arrFrequencies_l, Base::_iHighestK, mIDsAsIdx);
				}
				if (bOverwrite) {
					remove(sLibFile.c_str());
					Utilities::copyFile(sTempFile, fOutfile);
				}


				ofstream frequencyFile(fOutfile + "_f.txt");
				for (uint32_t j = 0; j < iIdxCounter; ++j) {
					frequencyFile << Utilities::checkIfInMap(mIdxToName, j)->second << "\t";
					frequencyFile << arrFrequencies_l[j * Base::_iHighestK];
					for (int32_t k = 1; k < Base::_iHighestK; ++k) {
						frequencyFile << "\t" << arrFrequencies_l[j * Base::_iHighestK + k];
					}
					frequencyFile << endl;
				}
			}
			catch (...) {
				cerr << "ERROR: in: " << __PRETTY_FUNCTION__ << endl; throw;
			}
		}


		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Update an existing library with a fasta file
		void UpdateFromFasta(const string& contentFile, const string& sLibFile, const string& sDirectory, string fOutFile, const bool& bOverwrite, const uint64_t& iMemory, const float& fPercentageOfThrowAway, const pair<unordered_map<uint32_t, uint32_t>, unordered_map<uint32_t, uint32_t>>& mapsForDummys) {
			try {

				// test if files exists
				if (!ifstream(contentFile) || !ifstream(sLibFile)) {
					throw runtime_error("One of the files does not exist");
				}

				if (bOverwrite) {
					fOutFile = sLibFile;
				}

				// read new content file
				uint32_t iIdxCounter = 1;
				unordered_map<uint32_t, uint32_t> mIDsAsIdx; mIDsAsIdx[0] = 0;
				unordered_map<uint32_t, string> mIdxToName; mIdxToName[0] = "non_unique";
				unordered_map<string, uint32_t> mAccToID;
				int64_t iAvailableMemory = static_cast<int64_t>(iMemory);
				ifstream content(contentFile);
				string sDummy = "";
				bool bTaxIdsAsStrings = false;
				while (getline(content, sDummy)) {
					if (sDummy != "") {
						const auto& line = Utilities::split(sDummy, '\t');
						if (line.size() >= 5 && !bTaxIdsAsStrings) {
							bTaxIdsAsStrings = true;
						}
						if (line.size() >= 4) {
							mIdxToName[iIdxCounter] = line[0];
							iAvailableMemory -= sizeof(char) * line[0].length();
							const auto& vAccessionNumbers = Utilities::split(line[3], ';');
							if (bTaxIdsAsStrings) {
								mIDsAsIdx[stoul(line[4])] = iIdxCounter;
								for (const auto& acc : vAccessionNumbers) {
									mAccToID.insert(make_pair(acc, stoul(line[4])));
									iAvailableMemory -= sizeof(char) * acc.length();
								}
							}
							else {
								mIDsAsIdx[stoul(line[1])] = iIdxCounter;
								for (const auto& acc : vAccessionNumbers) {
									mAccToID.insert(make_pair(acc, stoul(line[1])));
									iAvailableMemory -= sizeof(char) * acc.length();
								}
							}
							++iIdxCounter;
						}
						else {
							throw runtime_error("Content file contains less than 4 columns, it may be damaged...");
						}
					}
				}
				// hash tables do cost some memory!
				iAvailableMemory -= Utilities::calculateSizeInByteOfUnorderedMap(mIDsAsIdx) + Utilities::calculateSizeInByteOfUnorderedMap(mIdxToName) + Utilities::calculateSizeInByteOfUnorderedMap(mAccToID);

				if (iAvailableMemory < 0) {
					cerr << "WARNING: Not enough memory given, the content file and/or number of taxa is quite large. Trying with 1GB but may lead to bad_alloc errors..." << endl;
					iAvailableMemory = static_cast<int64_t>(1024ull * 1024ull * 1024ull);
				}

				// Create new index
				string sTempFile = Base::_sTemporaryPath + "_tempUpdate_out_" + to_string(Base::_iNumOfCall);
				{
					// get Size of Lib and open it for reading
					fstream fLibInfo(sLibFile + "_info.txt", ios::in);
					uint64_t iSizeOfLib = 0;
					fLibInfo >> iSizeOfLib;
					fLibInfo.close();
					unique_ptr<stxxlFile> stxxlLibFile(new stxxlFile(sLibFile, stxxl::file::RDONLY));
					unique_ptr<const vecType> vLibIn(new vecType(stxxlLibFile.get(), iSizeOfLib));
					typename vecType::bufreader_type vCBuff(*vLibIn);

					Utilities::checkIfFileCanBeCreated((bOverwrite) ? sTempFile : fOutFile);
					unique_ptr<stxxlFile> stxxlOutVec(new stxxlFile((bOverwrite) ? sTempFile : fOutFile, stxxl::file::RDWR));
					unique_ptr<vecType> vOutVec(new vecType(stxxlOutVec.get(), 0));
					typename vecType::bufwriter_type vNCBuff(*vOutVec);

					// add kMers from fasta
					Utilities::checkIfFileCanBeCreated(Base::_sTemporaryPath + "_tempUpdate_" + to_string(Base::_iNumOfCall));

					Build<vecType, elemType> brick(Base::_sTemporaryPath, Base::_iNumOfCall, Base::_iNumOfThreads, iAvailableMemory / (sizeof(elemType)), iIdxCounter);

					size_t overallCharsRead = 0;
					unique_ptr<vecType> dummy;

					auto filesAndSize = Utilities::gatherFilesFromPath(sDirectory);
					for (auto& fileName : filesAndSize.first) {
						if (Base::_bVerbose) {
							cout << "OUT: Current file: " << fileName.first << endl;
						}

						// check if gzipped
						bool isGzipped = fileName.second;

						if (isGzipped) {
							if (Base::_bVerbose) {
								cout << "OUT: File is gzipped, no progress output can be shown." << endl;
							}
							igzstream fastaFile_gz(fileName.first.c_str());
							this->readFasta(fastaFile_gz, mAccToID, brick, dummy, 0, overallCharsRead, filesAndSize.second, fPercentageOfThrowAway);
						}
						else {
							ifstream fastaFile(fileName.first);
							fastaFile.seekg(0, fastaFile.end);
							const uint64_t& iFileLength = fastaFile.tellg();
							fastaFile.seekg(0, fastaFile.beg);
							this->readFasta(fastaFile, mAccToID, brick, dummy, iFileLength, overallCharsRead, filesAndSize.second, fPercentageOfThrowAway);
						}
					}

					// Finalize
					brick.IntToExtPart();

					unique_ptr<uint64_t[]> arrFrequencies_(new uint64_t[iIdxCounter * Base::_iHighestK]);
					for (uint64_t i = 0; i < iIdxCounter * Base::_iHighestK; ++i) {
						arrFrequencies_[i] = 0;
					}

					const uint64_t& iSizeOfFinalIndex = brick.mergeTemporaries(Base::_sTemporaryPath + "_tempUpdate_" + to_string(Base::_iNumOfCall));
					{ // new scope for merging so that the temporary can be removed afterwards
						unique_ptr<stxxlFile> stxxlTempVec(new stxxlFile(Base::_sTemporaryPath + "_tempUpdate_" + to_string(Base::_iNumOfCall), stxxl::file::RDONLY));
						unique_ptr<vecType> vTempVec(new vecType(stxxlTempVec.get(), iSizeOfFinalIndex));

						// Merge existing db and new one
						const auto& vOutSize = this->merge(vNCBuff, vCBuff, iSizeOfLib, vTempVec->cbegin(), vTempVec->cend(), arrFrequencies_, mIDsAsIdx, mapsForDummys);
						vOutVec->resize(vOutSize, true);


						//size_t overallCharsRead = 0;
						//Build dummy;
						//if (sDirectory.back() == '/') {
						//	auto filesAndSize = Utilities::gatherFilesFromPath(sDirectory);
						//	for (auto& fileName : filesAndSize.first) {
						//		if (_bVerbose) {
						//			cout << "OUT: Current file: " << fileName << endl;
						//		}
						//		ifstream fastaFile(fileName);
						//		fastaFile.seekg(0, fastaFile.end);
						//		const uint64_t& iFileLength = fastaFile.tellg();
						//		fastaFile.seekg(0, fastaFile.beg);
						//		readFasta(fastaFile, mAccToID, dummy, vTempVec, iFileLength, overallCharsRead, filesAndSize.second, fPercentageOfThrowAway);
						//	}
						//}
						//else {
						//	ifstream fastaFile;
						//	//fastaFile.exceptions(std::ifstream::failbit | std::ifstream::badbit); 
						//	fastaFile.open(sDirectory);
						//	if (_bVerbose) {
						//		cout << "OUT: Current file: " << sDirectory << endl;
						//	}
						//	fastaFile.seekg(0, fastaFile.end);
						//	const uint64_t& iFileLength = fastaFile.tellg();
						//	fastaFile.seekg(0, fastaFile.beg);
						//	readFasta(fastaFile, mAccToID, dummy, vTempVec, iFileLength, overallCharsRead, iFileLength, fPercentageOfThrowAway);
						//}


						//stxxl::sort(vTempVec->begin(), vTempVec->end(), SCompareStructForSTXXLSort(), iMemory);
						/*
						auto funcForUnique = [](packedBigPair& a, packedBigPair& b) {
							if (a == b) {
								return true;
							}
							return false;
						};
						contentVecType_32p::iterator newEnd = std::unique(vTempVec->begin(), vTempVec->end(), funcForUnique);
						vTempVec->resize(newEnd - vTempVec->begin(), true);*/ // will be done by merge
					}

					remove((Base::_sTemporaryPath + "_tempUpdate_" + to_string(Base::_iNumOfCall)).c_str());
					remove((Base::_sTemporaryPath + "_tempUpdate_" + to_string(Base::_iNumOfCall) + "_info.txt").c_str());

					ofstream fOutInfo(fOutFile + "_info.txt");
					fOutInfo << vOutVec->size();
					if (is_same<vecType, contentVecType_128>::value) {
						fOutInfo << endl << 128;
					}

					ofstream outFile(fOutFile + "_f.txt");
					for (uint32_t j = 0; j < iIdxCounter; ++j) {
						outFile << Utilities::checkIfInMap(mIdxToName, j)->second << "\t";
						outFile << arrFrequencies_[j * Base::_iHighestK];
						for (int32_t k = 1; k < Base::_iHighestK; ++k) {
							outFile << "\t" << arrFrequencies_[j * Base::_iHighestK + k];
						}
						outFile << endl;
					}

					vOutVec->export_files("_");
					
					Trie<intType> T(static_cast<int8_t>(((is_same<vecType, contentVecType_128>::value) ? HIGHESTPOSSIBLEK : 12)), static_cast<int8_t>(Base::_iMinK), 6);
					T.SaveToStxxlVec(vOutVec.get(), fOutFile);
				}
				
				// "overwrite" old index
				if (bOverwrite) {
					remove(sLibFile.c_str());
					Utilities::copyFile(sTempFile, fOutFile);
				}
				
			}
			catch (...) {
				cerr << "ERROR: in: " << __PRETTY_FUNCTION__ << endl; throw;
			}
		}

	};

}