// ffMixer application.
//

#include "ffMixer.h"

int main(int argc, char* argv[])
{
	int ret = 0;
	
	ret = ff_mix_audio("music1.mp3", "music2.mp3");
	return 0;
}

