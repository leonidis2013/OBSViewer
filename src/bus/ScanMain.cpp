// obsviewer-scan: scans one plugin file in its own process so a plugin that
// crashes or hangs while being inspected can't take OBS down with it.
#include "bus/Engine.hpp"

int main(int argc, char **argv)
{
	return obsviewer::bus::runScanHelper(argc, argv);
}
