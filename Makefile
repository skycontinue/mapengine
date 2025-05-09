
all: android-demo

.PHONY: clean
.PHONY: clean-android
.PHONY: clean-osx
.PHONY: clean-xcode
.PHONY: clean-ios
.PHONY: clean-rpi
.PHONY: clean-linux
.PHONY: clean-benchmark
.PHONY: clean-shaders
.PHONY: clean-tizen-arm
.PHONY: clean-tizen-x86
.PHONY: android
.PHONY: osx
.PHONY: xcode
.PHONY: ios
.PHONY: ios-sim
.PHONY: ios-static
.PHONY: ios-static-sim
.PHONY: ios-static-lib
.PHONY: ios-static-lib-sim
.PHONY: ios-static-lib-universal
.PHONY: ios-swift
.PHONY: ios-swift-sim
.PHONY: ios-framework
.PHONY: ios-framework-sim
.PHONY: ios-framework-universal
.PHONY: ios-docs
.PHONY: rpi
.PHONY: linux
.PHONY: benchmark
.PHONY: tests
.PHONY: cmake-osx
.PHONY: cmake-xcode
.PHONY: cmake-ios
.PHONY: cmake-rpi
.PHONY: cmake-linux

ANDROID_BUILD_DIR = platforms/android/tangram/build
OSX_BUILD_DIR = build/osx
OSX_XCODE_BUILD_DIR = build/xcode
IOS_BUILD_DIR = build/ios
IOS_DOCS_BUILD_DIR = build/ios-docs
RPI_BUILD_DIR = build/rpi
LINUX_BUILD_DIR = build/linux
TESTS_BUILD_DIR = build/tests
BENCH_BUILD_DIR = build/bench
TIZEN_ARM_BUILD_DIR = build/tizen-arm
TIZEN_X86_BUILD_DIR = build/tizen-x86

#检查系统上是否安装了xcpretty,它是一个用于格式化xcodebuild输出的命令行工具。如果没有安装，将XCPRETTY变量设置为空字符串。如果已经安装，则将XCPRETTY变量设置为一个管道命令，用于将输出传递给xcpretty，然后退出前一个命令的原始退出状态（$${PIPESTATUS[0]}）
ifeq (, $(shell which xcpretty))
	XCPRETTY =
else
	XCPRETTY = | xcpretty && exit $${PIPESTATUS[0]}
endif

# Default build type is Release
BUILD_TYPE ?= Release

IOS_SIM_ARCH ?= $(shell uname -m)

BENCH_CMAKE_PARAMS = \
	-DTANGRAM_BUILD_BENCHMARKS=1 \
	-DCMAKE_BUILD_TYPE=Release \
	${CMAKE_OPTIONS}

TESTS_CMAKE_PARAMS = \
	-DTANGRAM_BUILD_TESTS=1 \
	-DCMAKE_BUILD_TYPE=Debug \
	${CMAKE_OPTIONS}

IOS_CMAKE_PARAMS = \
	-DTANGRAM_PLATFORM=ios \
	-DCMAKE_SYSTEM_NAME=iOS \
	-DCMAKE_XCODE_GENERATE_SCHEME=0 \
	-G Xcode \
	${CMAKE_OPTIONS}

OSX_XCODE_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=osx \
	-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING="10.9" \
	-G Xcode \
	${CMAKE_OPTIONS}

OSX_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=osx \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

RPI_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=rpi \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

LINUX_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=linux \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

TIZEN_PROFILE ?= mobile
TIZEN_VERSION ?= 3.0

TIZEN_ARM_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTIZEN_SDK=$$TIZEN_SDK \
	-DTIZEN_SYSROOT=$$TIZEN_SDK/platforms/tizen-${TIZEN_VERSION}/${TIZEN_PROFILE}/rootstraps/${TIZEN_PROFILE}-${TIZEN_VERSION}-device.core \
	-DTIZEN_DEVICE=1 \
	-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_DIR}/tizen.toolchain.cmake \
	-DTANGRAM_PLATFORM=tizen \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

TIZEN_X86_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTIZEN_SDK=$$TIZEN_SDK \
	-DTIZEN_SYSROOT=$$TIZEN_SDK/platforms/tizen-${TIZEN_VERSION}/${TIZEN_PROFILE}/rootstraps/${TIZEN_PROFILE}-${TIZEN_VERSION}-emulator.core \
	-DTIZEN_DEVICE=0 \
	-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_DIR}/tizen.toolchain.cmake \
	-DTANGRAM_PLATFORM=tizen \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

clean: clean-android clean-osx clean-ios clean-rpi clean-tests clean-xcode clean-linux clean-shaders \
	clean-tizen-arm clean-tizen-x86

clean-android:
	@echo "skyway clean-android"
	rm -rf platforms/android/build
	rm -rf platforms/android/tangram/build
	rm -rf platforms/android/tangram/.externalNativeBuild
	rm -rf platforms/android/demo/build

clean-osx:
	rm -rf ${OSX_BUILD_DIR}

clean-ios:
	rm -rf ${IOS_BUILD_DIR}

clean-rpi:
	rm -rf ${RPI_BUILD_DIR}

clean-linux:
	rm -rf ${LINUX_BUILD_DIR}

clean-xcode:
	rm -rf ${OSX_XCODE_BUILD_DIR}

clean-tests:
	rm -rf ${TESTS_BUILD_DIR}

clean-benchmark:
	rm -rf ${BENCH_BUILD_DIR}

clean-shaders:
	rm -rf core/generated/*.h

clean-tizen-arm:
	rm -rf ${TIZEN_ARM_BUILD_DIR}

clean-tizen-x86:
	rm -rf ${TIZEN_X86_BUILD_DIR}

android: android-demo
	@echo "run: 'adb install -r platforms/android/demo/build/outputs/apk/debug/demo-debug.apk'"

android-sdk:
	@cd platforms/android/ && \
	./gradlew tangram:assembleRelease

android-demo:clean-android
	@cd platforms/android/ && \
	./gradlew demo:assembleDebug

osx: cmake-osx
	cmake --build ${OSX_BUILD_DIR}

cmake-osx:
	cmake -H. -B${OSX_BUILD_DIR} ${OSX_CMAKE_PARAMS}

xcode: cmake-xcode
	xcodebuild -target tangram -project ${OSX_XCODE_BUILD_DIR}/tangram.xcodeproj -configuration ${BUILD_TYPE} ${XCPRETTY}

cmake-xcode:
	cmake -H. -B${OSX_XCODE_BUILD_DIR} ${OSX_XCODE_CMAKE_PARAMS}

ios: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemo -configuration ${BUILD_TYPE} -sdk iphoneos ${XCPRETTY}

ios-sim: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemo -configuration ${BUILD_TYPE} -sdk iphonesimulator -arch ${IOS_SIM_ARCH} ${XCPRETTY}

ios-swift: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemoSwift -configuration ${BUILD_TYPE} -sdk iphoneos ${XCPRETTY}

ios-swift-sim: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemoSwift -configuration ${BUILD_TYPE} -sdk iphonesimulator -arch ${IOS_SIM_ARCH} ${XCPRETTY}

ios-xcode: cmake-ios
	open platforms/ios/Tangram.xcworkspace

ios-docs:
ifeq (, $(shell which jazzy))
	$(error "Please install jazzy by running 'gem install jazzy'")
endif
	@mkdir -p ${IOS_DOCS_BUILD_DIR}
	@cd platforms/ios && \
	jazzy --config jazzy.yml

cmake-ios:
	cmake -H. -B${IOS_BUILD_DIR} ${IOS_CMAKE_PARAMS}

ios-framework: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramMap -configuration ${BUILD_TYPE} -sdk iphoneos ${XCPRETTY}

ios-framework-sim: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramMap -configuration ${BUILD_TYPE} -sdk iphonesimulator ${XCPRETTY}

ios-xcframework: ios-framework ios-framework-sim
	rm -rf ${IOS_BUILD_DIR}/${BUILD_TYPE}/TangramMap.xcframework
	xcodebuild -create-xcframework \
		-framework ${IOS_BUILD_DIR}/${BUILD_TYPE}-iphoneos/TangramMap.framework \
		-framework ${IOS_BUILD_DIR}/${BUILD_TYPE}-iphonesimulator/TangramMap.framework \
		-output ${IOS_BUILD_DIR}/${BUILD_TYPE}/TangramMap.xcframework

ios-static: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemo-static -configuration ${BUILD_TYPE} -sdk iphoneos ${XCPRETTY}

ios-static-sim: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme TangramDemo-static -configuration ${BUILD_TYPE} -sdk iphonesimulator -arch ${IOS_SIM_ARCH} ${XCPRETTY}

ios-static-lib: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme tangram-static -configuration ${BUILD_TYPE} -sdk iphoneos ${XCPRETTY}

ios-static-lib-sim: cmake-ios
	xcodebuild -workspace platforms/ios/Tangram.xcworkspace -scheme tangram-static -configuration ${BUILD_TYPE} -sdk iphonesimulator ${XCPRETTY}

ios-static-xcframework: ios-static-lib ios-static-lib-sim
	rm -rf ${IOS_BUILD_DIR}/${BUILD_TYPE}/TangramMapStatic.xcframework
	xcodebuild -create-xcframework \
		-library ${IOS_BUILD_DIR}/${BUILD_TYPE}-iphoneos/libtangram-static.a \
		-headers ${IOS_BUILD_DIR}/Headers \
		-library ${IOS_BUILD_DIR}/${BUILD_TYPE}-iphonesimulator/libtangram-static.a \
		-headers ${IOS_BUILD_DIR}/Headers \
		-output ${IOS_BUILD_DIR}/${BUILD_TYPE}/TangramMapStatic.xcframework

rpi: cmake-rpi
	cmake --build ${RPI_BUILD_DIR}

cmake-rpi:
	cmake -H. -B${RPI_BUILD_DIR} ${RPI_CMAKE_PARAMS}

linux: cmake-linux
	cmake --build ${LINUX_BUILD_DIR}

cmake-linux:
	cmake -H. -B${LINUX_BUILD_DIR} ${LINUX_CMAKE_PARAMS}

tizen-arm: cmake-tizen-arm
	cmake --build ${TIZEN_ARM_BUILD_DIR}

cmake-tizen-arm:
	cmake -H. -B${TIZEN_ARM_BUILD_DIR} ${TIZEN_ARM_CMAKE_PARAMS}

tizen-x86: cmake-tizen-x86
	cmake --build ${TIZEN_X86_BUILD_DIR}

cmake-tizen-x86:
	cmake -H. -B${TIZEN_X86_BUILD_DIR} ${TIZEN_X86_CMAKE_PARAMS}

tests:
	cmake -H. -B${TESTS_BUILD_DIR} ${TESTS_CMAKE_PARAMS}
	cmake --build ${TESTS_BUILD_DIR}

benchmark:
	cmake -H. -B${BENCH_BUILD_DIR} ${BENCH_CMAKE_PARAMS}
	cmake --build ${BENCH_BUILD_DIR}

format:
	@for file in `git diff --diff-filter=ACMRTUXB --name-only -- '*.cpp' '*.h'`; do \
		if [[ -e $$file ]]; then clang-format -i $$file; fi \
	done
	@echo "format done on `git diff --diff-filter=ACMRTUXB --name-only -- '*.cpp' '*.h'`"

### Android Helpers
android-install:
	@adb install -r platforms/android/demo/build/outputs/apk/demo-debug.apk
