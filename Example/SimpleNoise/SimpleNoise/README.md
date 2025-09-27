Even this is not working:

```c++
#define NOMINMAX
#include <cstdint>
#include <openvdb/openvdb.h>

int main() {
	openvdb::initialize();
	auto grid = openvdb::FloatGrid::create(0.0f);
	grid->setName("density");
	return 0;
}
```