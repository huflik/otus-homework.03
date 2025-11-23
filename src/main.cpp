#include "customallocator.h"
#include "custommath.h"
#include "customvector.h"

constexpr int chunkElems = 10;

int main() {

    try {
    	using Pair = std::pair<const int, int>;

		std::map<int, int> map;
		for(int i=0; i < chunkElems; ++i) {
			map.emplace(i,custmath::factorial(i));
		}

		std::map<int, int, std::less<int>, CustomAllocator<Pair, chunkElems>> mapAlloc;
		for(int i = 0; i < chunkElems; ++i) {
			mapAlloc.emplace(i, custmath::factorial(i));
		}

		for(const auto& [number, factorial] : mapAlloc) {
			std::cout << number << " " << factorial << std::endl;
		}

		SimpleVector<int> vector;
		for(int i = 0; i < chunkElems; ++i) {
			vector.PushBack(i);
		}

		SimpleVector<int, CustomAllocator<int, chunkElems>> vectorAlloc;
		for(int i = 0; i < chunkElems; ++i) {
			vectorAlloc.PushBack(i);
		}

		for(const auto& i: vectorAlloc) {
			std::cout << i << std::endl;
		}


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
