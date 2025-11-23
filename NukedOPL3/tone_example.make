## install ffplay for playback of raw file
sudo apt update
sudo apt install ffmpeg

g++ -o tone_example tone_example.c opl3.c -lm -o tone_example.exe

./tone_example.exe
ffmpeg -f s16le -ar 49716 -ac 2 -i tone_example.raw -codec:a libmp3lame -b:a 320k tone_example.mp3 -y
