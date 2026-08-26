test:
	scons platform=linux arch=x86_64 target=template_debug test
	./tests/probe_batch_test

install-steam-audio:
	curl -s https://api.github.com/repos/ValveSoftware/steam-audio/releases/latest \
		| grep -E 'browser_download.*steamaudio_[0-9\.]+\.zip' \
		| cut -d : -f 2,3 | tr -d \" | wget -O src/lib/steamaudio.zip -i -
	unzip src/lib/steamaudio.zip -d src/lib/
	rm src/lib/steamaudio.zip
	cp src/lib/steamaudio/lib/linux-x64/* project/addons/godot-steam-audio/bin/
	cp src/lib/steamaudio/lib/windows-x64/* project/addons/godot-steam-audio/bin/
	cp src/lib/steamaudio/lib/osx/* project/addons/godot-steam-audio/bin/
	cp src/lib/steamaudio/lib/android-armv8/* project/addons/godot-steam-audio/bin/android/arm64
	cp src/lib/steamaudio/lib/android-x64/* project/addons/godot-steam-audio/bin/android/x86_64
	cp src/lib/steamaudio/lib/ios/* project/addons/godot-steam-audio/bin/ios/

release:
	scons platform=android arch=arm64 target=template_release && scons platform=android arch=x86_64 target=template_release && \
		scons platform=android arch=arm64 target=template_debug && scons platform=android arch=x86_64 target=template_debug && \
		scons platform=linux target=template_debug && scons platform=windows target=template_debug && \
		scons platform=linux target=template_release && scons platform=windows target=template_release
	mkdir godot-steam-audio-demo
	mkdir godot-steam-audio
	cp -r ./project/* ./godot-steam-audio-demo
	rm -rf ./godot-steam-audio-demo/addons/godot-steam-audio/bin/libphonon.so.dbg
	cp -r ./godot-steam-audio-demo/addons ./godot-steam-audio

macos-release:
	scons platform=macos target=template_debug && scons platform=macos target=template_release
	mkdir godot-steam-audio-demo
	mkdir godot-steam-audio
	cp -r ./project/* ./godot-steam-audio-demo
	rm -rf ./godot-steam-audio-demo/addons/godot-steam-audio/bin/libphonon.so.dbg
	cp -r ./godot-steam-audio-demo/addons ./godot-steam-audio

ios-deps:
	# Build pffft for iOS arm64
	IOS_SDK=$$(xcrun --sdk iphoneos --show-sdk-path) && \
		xcrun --sdk iphoneos clang -arch arm64 -isysroot "$$IOS_SDK" -miphoneos-version-min=12.0 -O2 -DPFFFT_ENABLE_NEON \
			-c src/lib/pffft/pffft.c -o src/lib/pffft/pffft.o && \
		xcrun --sdk iphoneos clang -arch arm64 -isysroot "$$IOS_SDK" -miphoneos-version-min=12.0 -O2 -DPFFFT_ENABLE_NEON \
			-c src/lib/pffft/pffft_common.c -o src/lib/pffft/pffft_common.o && \
		ar rcs project/addons/godot-steam-audio/bin/ios/libpffft.a src/lib/pffft/pffft.o src/lib/pffft/pffft_common.o
	# Build libmysofa for iOS arm64
	IOS_SDK=$$(xcrun --sdk iphoneos --show-sdk-path) && \
		cmake -S src/lib/libmysofa -B src/lib/libmysofa/build-ios \
			-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_SYSROOT="$$IOS_SDK" \
			-DBUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF && \
		cmake --build src/lib/libmysofa/build-ios --config Release
	cp src/lib/libmysofa/build-ios/src/libmysofa.a project/addons/godot-steam-audio/bin/ios/

ios-release: ios-deps
	scons platform=ios arch=arm64 target=template_debug && scons platform=ios arch=arm64 target=template_release
	cp src/lib/godot-cpp/bin/libgodot-cpp.ios.template_debug.arm64.a project/addons/godot-steam-audio/bin/ios/
	cp src/lib/godot-cpp/bin/libgodot-cpp.ios.template_release.arm64.a project/addons/godot-steam-audio/bin/ios/
	mkdir -p godot-steam-audio-demo
	mkdir -p godot-steam-audio
	cp -r ./project/* ./godot-steam-audio-demo
	rm -rf ./godot-steam-audio-demo/addons/godot-steam-audio/bin/libphonon.so.dbg
	cp -r ./godot-steam-audio-demo/addons ./godot-steam-audio
