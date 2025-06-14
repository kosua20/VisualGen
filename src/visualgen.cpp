#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <sstream>

#if 0
	#include <filesystem>
	namespace fs = std::filesystem;
#else
	#define GHC_FILESYSTEM_IMPLEMENTATION
	#include "filesystem.hpp"
	namespace fs = ghc::filesystem;
#endif

// --------------------------------------------------------------------------------
// Simon Rodriguez, June 2025
// --------------------------------------------------------------------------------
// TODO:
// * GUID for the project?
// * additional prefix/suffix strings for the vcxproj (and vcxproj.filters?)
// --------------------------------------------------------------------------------

constexpr std::string helpStr = "visualgen local/path/to/dir ProjectName \"cpp,c\" \"h,hpp\"";

// --------------------------------------------------------------------------------
//	String and path utilities
// --------------------------------------------------------------------------------

void replace(std::string & source, const std::string & fromString, const std::string & toString) {
	std::string::size_type nextPos = 0;
	const size_t fromSize		   = fromString.size();
	const size_t toSize			   = toString.size();
	while((nextPos = source.find(fromString, nextPos)) != std::string::npos) {
		source.replace(nextPos, fromSize, toString);
		nextPos += toSize;
	}
}

std::string trim(const std::string & str, const std::string & del) {
	const size_t firstNotDel = str.find_first_not_of(del);
	if(firstNotDel == std::string::npos) {
		return "";
	}
	const size_t lastNotDel = str.find_last_not_of(del);
	return str.substr(firstNotDel, lastNotDel - firstNotDel + 1);
}

std::vector<std::string> split(const std::string & str, const std::string & delimiter, bool skipEmpty){
	// Delimiter is empty, using space as a delimiter.
	std::string subdelimiter = " ";
	if(!delimiter.empty()){
		// Only the first character of the delimiter will be used.
		subdelimiter = delimiter.substr(0,1);
	}
	std::stringstream sstr(str);
	std::string value;
	std::vector<std::string> tokens;
	while(std::getline(sstr, value, subdelimiter[0])) {
		if(!skipEmpty || !value.empty()) {
			tokens.emplace_back(value);
		}
	}
	return tokens;
}

std::unordered_set<std::string> extractExtensions(const std::string& extensionList){
	std::vector<std::string> extensions = split(trim(extensionList, "\""), ",", true);

	std::unordered_set<std::string> result;
	for(const std::string& extension : extensions){
		std::string extensionCleaned = trim(extension, ". ");
		if(extensionCleaned.empty())
			continue;
		result.insert("." + extensionCleaned);
	}
	return result;
}

void collectDirectoriesAlongPath(const fs::path& path, std::unordered_set<std::string>& directories){
	fs::path currentPath = path;
	// Stop when we reach the root, its own parent.
	while(currentPath.has_parent_path() && (currentPath.root_directory() != currentPath)){
		currentPath = currentPath.parent_path();
		std::string pathStr = currentPath.string();
		replace(pathStr, "/", "\\");
		// Skip root or empty.
		if(!pathStr.empty() && (pathStr != "\\") ){
			auto res = directories.insert(pathStr);
			// If the directory was already encountered, skip.
			if(!res.second){
				break;
			}
		}
	}
}

// --------------------------------------------------------------------------------
//	Go go go
// --------------------------------------------------------------------------------

int main(int argc, char** argv){

	if(argc < 3 || argc > 5){
		std::cout << helpStr << std::endl;
		return 0;
	}

	// Parameters
	const fs::path inputDirPath = fs::path(argv[1]);
	const std::string projectName = argv[2];
	const std::string compileExtensionsList = argc > 3 ? argv[3] : "";
	const std::string includeExtensionsList = argc > 4 ? argv[4] : "";
	const fs::path outputVcxprojPath = inputDirPath / (projectName + ".vcxproj");
	const fs::path outputFilterPath = inputDirPath / (projectName + ".vcxproj.filters");
	const std::string projectUUID = projectName;

	const std::unordered_set<std::string> compileExtensions = extractExtensions(compileExtensionsList);
	const std::unordered_set<std::string> includeExtensions = extractExtensions(includeExtensionsList);
	const bool noExtensionFilter = compileExtensions.empty() && includeExtensions.empty();

	std::cout << "Processing " << inputDirPath.string() << " to " << outputVcxprojPath.string() << std::endl;

	
	// Collect file paths and directories
	std::vector<fs::path> compileFilePaths;
	std::vector<fs::path> includeFilePaths;
	std::unordered_set<std::string> directoryPaths;

	for(const auto& entry : fs::recursive_directory_iterator(inputDirPath)){
		if(!entry.is_regular_file()){
			continue;
		}
		const fs::path entryPath = relative(entry.path(), inputDirPath);
		const fs::path filename = entryPath.filename();
		const std::string entryName = filename.string();
		// Skip hidden
		if(entryName.empty() || entryName[0] == '.'){
			continue;
		}
		// Skip generated files.
		if(filename == outputVcxprojPath.filename() || filename == outputFilterPath.filename()){
			continue;
		}
		// If no filter, assume everything is compiled.
		if(noExtensionFilter || (compileExtensions.count(filename.extension()) != 0)){
			compileFilePaths.emplace_back(entryPath);
			collectDirectoriesAlongPath(entryPath, directoryPaths);
		}
		if(includeExtensions.count(filename.extension()) != 0){
			includeFilePaths.emplace_back(entryPath);
			collectDirectoriesAlongPath(entryPath, directoryPaths);
		}
	}

	// Sort filters from smallest to largest, that way a parent is always before its children.
	std::vector<std::string> filterPaths;
	filterPaths.insert(filterPaths.begin(), directoryPaths.begin(), directoryPaths.end());
	std::sort(filterPaths.begin(), filterPaths.end());
	std::sort(compileFilePaths.begin(), compileFilePaths.end());
	std::sort(includeFilePaths.begin(), includeFilePaths.end());

	// Generate .vcxproj
	{
		std::ofstream vcxproj(outputVcxprojPath);
		if(!vcxproj.is_open()){
			std::cout << "Error" << std::endl;
			return 1;
		}

		vcxproj << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
		vcxproj << "<Project DefaultTargets=\"Build\" ToolsVersion=\"4\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
		vcxproj << "\n";

		vcxproj << "<PropertyGroup Label=\"Globals\">\n";
		vcxproj << "\t<ProjectGuid>" << projectUUID << "</ProjectGuid>\n";
		vcxproj << "\t<RootNamespace>" << projectName << "</RootNamespace>\n";
		vcxproj << "</PropertyGroup>\n";
		vcxproj << "\n";

		if(!includeFilePaths.empty()){
			vcxproj << "<ItemGroup>\n";
			for(const fs::path& path : includeFilePaths){
				vcxproj << "\t<ClInclude Include=\"" << path.string() << "\" />\n";
			}
			vcxproj << "</ItemGroup>\n";
			vcxproj << "\n";

		}

		if(!compileFilePaths.empty()){
			vcxproj << "<ItemGroup>\n";
			for(const fs::path& path : compileFilePaths){
				vcxproj << "\t<ClCompile Include=\"" << path.string() << "\" />\n";
			}
			vcxproj << "</ItemGroup>\n";
			vcxproj << "\n";

		}

		vcxproj << "</Project>\n";

		vcxproj.close();
	}
	

	// Generate .vcxproj.filters
	{
		std::ofstream filters(outputFilterPath);
		if(!filters.is_open()){
			std::cout << "Error" << std::endl;
			return 1;
		}

		filters << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
		filters << "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
		filters << "\n";

		if(!directoryPaths.empty()){
			filters << "<ItemGroup>\n";
			for(const std::string& filter : filterPaths){
				filters << "\t<Filter Include=\"" << filter << "\">\n";
        		// optional: filters << "\t	<UniqueIdentifier>" << "0" << "</UniqueIdentifier>\n";
        		filters << "\t</Filter>\n";
			}
			filters << "</ItemGroup>\n";
			filters << "\n";
		}

		if(!includeFilePaths.empty()){
			filters << "<ItemGroup>\n";
			for(const fs::path& path : includeFilePaths){
				std::string filter = path.parent_path().string();
				replace(filter, "/", "\\");

				filters << "\t<ClInclude Include=\"" << path.string() << "\">\n";
				filters << "\t\t<Filter>" << filter << "</Filter>\n";
				filters << "\t</ClInclude>\n";
			}
			filters << "</ItemGroup>\n";
			filters << "\n";
		}

		if(!compileFilePaths.empty()){
			filters << "<ItemGroup>\n";
			for(const fs::path& path : compileFilePaths){
				std::string filter = path.parent_path().string();
				replace(filter, "/", "\\");

				filters << "\t<ClCompile Include=\"" << path.string() << "\">\n";
				filters << "\t\t<Filter>" << filter << "</Filter>\n";
				filters << "\t</ClCompile>\n";
			}
			filters << "</ItemGroup>\n";
			filters << "\n";
		}

		filters << "</Project>\n";

		filters.close();
	}

	return 0;

}