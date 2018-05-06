#include <libbsa/libbsa.h>
#include <filesystem>

namespace fs = std::experimental::filesystem;

namespace ckcmd {
namespace BSA {

	//TODO: exceptions
	class BSAFile {
		bsa_handle bh;
		fs::path path;
	public:

		BSAFile(const fs::path p) {
			open(p);
		}

		void open(const fs::path p) {
			unsigned int ret = bsa_open(&bh, p.string().c_str());
			path = p;
		}

		void close() {
			bsa_close(bh);
			path.clear();
		}

		const std::vector<std::string> assets(const std::string& regex = ".*") {
			const char * const * assetPaths;
			size_t size;
			bsa_get_assets(bh, regex.c_str(), &assetPaths, &size);
			return std::vector<std::string>(assetPaths, assetPaths + size);
		}

		bool find(const std::string& regex = "") {
			bool result = false;
			bsa_contains_asset(bh, regex.c_str(), &result);
			return result;
		}

		~BSAFile() {
			bsa_close(bh);
		}

	};

}
}

