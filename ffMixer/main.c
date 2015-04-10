// ffMixer application.
//

#include "ffMixer.h"
#include "ffFormat.h"

int main(int argc, char* argv[])
{
	int ret = 0;
	
	//ret = ff_mix_audio("music1.mp3", "music2.mp3");
	ret = ff_format_audio("c:\\music1.mp3", "c:\\music2.mp3");
	return 0;
}

