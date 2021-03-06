#include <iostream>
#include <Windows.h>
#include <Commdlg.h>
#include <String.h>
#include <winnt.h>
#include <imagehlp.h>
#include <vector>
#include <string>
#include <fstream>
#include <tchar.h>
#include <stdio.h>

using namespace std;

WORD fileType;

vector<string> names;

const vector<string> explode(const string& s, const char& c) {
	string buff{ "" };
	vector<string> v;

	for (auto n : s) {
		if (n != c) buff += n;
		else if (n == c && buff != "") {
			v.push_back(buff);
			buff = "";
		}
	}
	if (buff != "") v.push_back(buff);

	return v;
}

bool getImageFileHeaders(string fileName, IMAGE_NT_HEADERS& headers) {
	HANDLE fileHandle = CreateFile(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (fileHandle == INVALID_HANDLE_VALUE) return false;

	HANDLE imageHandle = CreateFileMapping(fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (imageHandle == 0) {
		CloseHandle(fileHandle);
		return false;
	}

	void* imagePtr = MapViewOfFile(imageHandle, FILE_MAP_READ, 0, 0, 0);
	if (!imagePtr) {
		CloseHandle(imageHandle);
		CloseHandle(fileHandle);
		return false;
	}

	PIMAGE_NT_HEADERS headersPtr = ImageNtHeader(imagePtr);
	if (!headersPtr) {
		UnmapViewOfFile(imagePtr);
		CloseHandle(imageHandle);
		CloseHandle(fileHandle);
		return false;
	}

	headers = *headersPtr;

	UnmapViewOfFile(imagePtr);
	CloseHandle(imageHandle);
	CloseHandle(fileHandle);

	return true;
}

void listDllFunctions(string sADllName, vector<string>& slListOfDllFunctions) {
	DWORD* dNameRVAs(0);
	DWORD* dNameRVAs2(0);
	_IMAGE_EXPORT_DIRECTORY* ImageExportDirectory;
	unsigned long cDirSize;
	_LOADED_IMAGE LoadedImage;
	string sName;
	slListOfDllFunctions.clear();
	if (MapAndLoad(sADllName.c_str(), 0, &LoadedImage, 1, 1)) {
		ImageExportDirectory = (_IMAGE_EXPORT_DIRECTORY*)ImageDirectoryEntryToData(LoadedImage.MappedAddress, false, IMAGE_DIRECTORY_ENTRY_EXPORT, &cDirSize);

		if (ImageExportDirectory != 0) {
			dNameRVAs = (DWORD*)ImageRvaToVa(LoadedImage.FileHeader, LoadedImage.MappedAddress, ImageExportDirectory->AddressOfNames, 0);

			for (size_t i = 0; i < ImageExportDirectory->NumberOfNames; i++) {
				sName = (char*)ImageRvaToVa(LoadedImage.FileHeader, LoadedImage.MappedAddress, dNameRVAs[i], 0);
				slListOfDllFunctions.push_back(sName);
			}
		}
		UnMapAndLoad(&LoadedImage);
	}
}

void generateDef(string name, vector<string> names) {
	std::fstream file;
	file.open(name + ".def", std::ios::out);
	file << "LIBRARY " << name << endl;
	file << "EXPORTS" << endl;

	for (unsigned int i = 0; i < names.size(); i++) {
		file << "\t" << names[i] << "=f" << names[i] << " @" << i + 1 << endl;
	}

	file.close();
}

void generateMainCpp(string name, vector<string> names) {
	int fileNameLength = name.size() + 6;
	std::fstream file;
	file.open("dllmain.cpp", std::ios::out);
	file << "#include <Windows.h>" << std::endl << std::endl;
	file << "#pragma region Proxy" << std::endl;

	file << "struct " << name << "_dll {" << std::endl << "\tHMODULE dll;" << std::endl;

	for (unsigned int i = 0; i < names.size(); i++) {
		file << "\tFARPROC o" << names[i] << ";" << std::endl;
	}
	file << "} " << name << ";" << std::endl << std::endl;

	file << "extern \"C\" {" << std::endl;

	if (fileType == IMAGE_FILE_MACHINE_AMD64) {
		file << "\tFARPROC PA = 0;" << std::endl;
		file << "\tint runASM();" << std::endl << std::endl;
		for (unsigned int i = 0; i < names.size(); i++) {
			file << "\tvoid f" << names[i] << "() { PA = " << name << ".o" << names[i] << "; runASM(); }" << std::endl;
		}
	}
	else {
		for (unsigned int i = 0; i < names.size(); i++) {
			file << "\tvoid f" << names[i] << "() { if _asm jmp[" << name << ".o" << names[i] << "] }" << std::endl;
		}
	}

	file << "}" << std::endl << std::endl;
	file << "void setupFunctions() {" << std::endl;
	for (unsigned int i = 0; i < names.size(); i++) {
		file << "\t" << name << ".o" << names[i] << " = GetProcAddress(" << name << ".dll, \"" << names[i] << "\");" << std::endl;
	}
	file << "}" << std::endl;
	file << "#pragma endregion" << std::endl;
	file << std::endl;

	file << "BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {" << std::endl;

	file << "\tswitch (ul_reason_for_call) {" << std::endl;
	file << "\tcase DLL_PROCESS_ATTACH:" << std::endl;
	file << "\t\tchar path[MAX_PATH];" << std::endl;
	file << "\t\tGetWindowsDirectory(path, sizeof(path));" << std::endl;
	file << std::endl;
	file << "\t\t// Example: \"\\\\System32\\\\version.dll\"" << std::endl;
	file << "\t\tstrcat_s(path, \"\\\\add manual path\\\\" << name << ".dll\");" << std::endl;
	file << "\t\t" << name << ".dll = LoadLibrary(path);" << std::endl;
	file << "\t\tsetupFunctions();" << std::endl;
	file << std::endl;
	file << "\t\t// Add here your code, I recommend you to create a thread" << std::endl;
	file << "\t\tbreak;" << std::endl;
	file << "\tcase DLL_PROCESS_DETACH:" << std::endl;
	file << "\t\tFreeLibrary(" << name << ".dll);" << std::endl;
	file << "\t\tbreak;" << std::endl;
	file << "\t}" << std::endl;
	file << "\treturn 1;" << std::endl;
	file << "}" << std::endl;

	file.close();
}

void generateAsm(string name) {
	std::fstream file;
	file.open(name + ".asm", std::ios::out);
	file << ".data" << endl;
	file << "extern PA : qword" << endl;
	file << ".code" << endl;
	file << "runASM proc" << endl;
	file << "jmp qword ptr [PA]" << endl;
	file << "runASM endp" << endl;
	file << "end" << endl;

	file.close();
}

int main(int argc, char* argv[]) {
	std::vector<std::string> args(argv, argv + argc);
	if (argc == 1) {
		std::cout << "Invalid arguments." << std::endl;
		return 0;
	}

	std::cout << "Starting..." << std::endl;

	IMAGE_NT_HEADERS headers;
	if (getImageFileHeaders(args[1], headers)) fileType = headers.FileHeader.Machine;

	vector<std::string> fileNameV = explode(args[1], '\\');
	std::string fileName = fileNameV[fileNameV.size() - 1];
	fileName = fileName.substr(0, fileName.size() - 4);
	std::cout << "Generating DLL Proxy for DLL " << fileName << "..." << std::endl;

	listDllFunctions(args[1], names);

	std::cout << "Generating DEF file..." << std::endl;
	generateDef(fileName, names);
	std::cout << "Generating CPP file..." << std::endl;
	generateMainCpp(fileName, names);

	if (fileType == IMAGE_FILE_MACHINE_AMD64) {
		std::cout << "Generating ASM file..." << std::endl;
		generateAsm(fileName);
	}

	std::cout << "Done!" << std::endl;
	system("pause");

	return 0;
}