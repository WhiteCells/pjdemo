### build

```sh
g++ -o sip_client main.c -I/usr/local/include -L/usr/local/lib -lpjsip-x86_64-pc-linux-gnu -lpj-x86_64-pc-linux-gnu -lpjlib-util-x86_64-pc-linux-gnu -lpjsua2-x86_64-pc-linux-gnu -lpjsua-x86_64-pc-linux-gnu -lpjmedia-x86_64-pc-linux-gnu -lpjnath-x86_64-pc-linux-gnu -lstdc++ -lm -luuid
```

```sh
g++ audio_stream.cpp -o audio_stream -lportaudio -lsndfile
``