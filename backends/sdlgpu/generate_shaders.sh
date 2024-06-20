# Rebuilds the shaders needed for the GPU cube test.
# Requires glslangValidator and spirv-cross, which can be obtained from the LunarG Vulkan SDK.

# On Windows, run this via Git Bash.

export MSYS_NO_PATHCONV=1

# SPIR-V
glslangValidator shader.vert -V -o shader.vert.spv --quiet
glslangValidator shader.frag -V -o shader.frag.spv --quiet
xxd -i shader.vert.spv | perl -w -p -e 's/\Aunsigned /const unsigned /;' > cube_vert.h
xxd -i shader.frag.spv | perl -w -p -e 's/\Aunsigned /const unsigned /;' > cube_frag.h
cat cube_vert.h cube_frag.h > testgpu_spirv.h

# Platform-specific compilation
if [[ "$OSTYPE" == "darwin"* ]]; then

    # MSL
    spirv-cross shader.vert.spv --msl --output shader.vert.metal
    spirv-cross shader.frag.spv --msl --output shader.frag.metal

    # Xcode
    generate_shaders()
    {
        fileplatform=$1
        compileplatform=$2
        sdkplatform=$3
        minversion=$4

        xcrun -sdk $sdkplatform metal -c -std=$compileplatform-metal1.1 -m$sdkplatform-version-min=$minversion -Wall -O3 -o ./shader.vert.air ./shader.vert.metal || exit $?
        xcrun -sdk $sdkplatform metal -c -std=$compileplatform-metal1.1 -m$sdkplatform-version-min=$minversion -Wall -O3 -o ./shader.frag.air ./shader.frag.metal || exit $?

        xcrun -sdk $sdkplatform metal-ar rc shader.vert.metalar shader.vert.air || exit $?
        xcrun -sdk $sdkplatform metal-ar rc shader.frag.metalar shader.frag.air || exit $?

        xcrun -sdk $sdkplatform metallib -o shader.vert.metallib shader.vert.metalar || exit $?
        xcrun -sdk $sdkplatform metallib -o shader.frag.metallib shader.frag.metalar || exit $?

        xxd -i shader.vert.metallib | perl -w -p -e 's/\Aunsigned /const unsigned /;' >./shader.vert_$fileplatform.h
        xxd -i shader.frag.metallib | perl -w -p -e 's/\Aunsigned /const unsigned /;' >./shader.frag_$fileplatform.h

        rm -f shader.vert.air shader.vert.metalar shader.vert.metallib
        rm -f shader.frag.air shader.frag.metalar shader.frag.metallib
    }

    generate_shaders macos macos macosx 10.11
    generate_shaders ios ios iphoneos 8.0
    generate_shaders iphonesimulator ios iphonesimulator 8.0
    generate_shaders tvos ios appletvos 9.0
    generate_shaders tvsimulator ios appletvsimulator 9.0

    # Bundle together one mega-header
    rm -f testgpu_metallib.h
    echo "#if defined(SDL_PLATFORM_IOS)" >> testgpu_metallib.h
        echo "#if TARGET_OS_SIMULATOR" >> testgpu_metallib.h
            cat shader.vert_iphonesimulator.h >> testgpu_metallib.h
            cat shader.frag_iphonesimulator.h >> testgpu_metallib.h
        echo "#else" >> testgpu_metallib.h
            cat shader.vert_ios.h >> testgpu_metallib.h
            cat shader.frag_ios.h >> testgpu_metallib.h
        echo "#endif" >> testgpu_metallib.h
    echo "#elif defined(SDL_PLATFORM_TVOS)" >> testgpu_metallib.h
        echo "#if TARGET_OS_SIMULATOR" >> testgpu_metallib.h
            cat shader.vert_tvsimulator.h >> testgpu_metallib.h
            cat shader.frag_tvsimulator.h >> testgpu_metallib.h
        echo "#else" >> testgpu_metallib.h
            cat shader.vert_tvos.h >> testgpu_metallib.h
            cat shader.frag_tvos.h >> testgpu_metallib.h
        echo "#endif" >> testgpu_metallib.h
    echo "#else" >> testgpu_metallib.h
        cat shader.vert_macos.h >> testgpu_metallib.h
        cat shader.frag_macos.h >> testgpu_metallib.h
    echo "#endif" >> testgpu_metallib.h

    # Clean up
    rm -f shader.vert.metal shader.frag.metal
    rm -f shader.vert_macos.h shader.frag_macos.h
    rm -f shader.vert_iphonesimulator.h shader.frag_iphonesimulator.h
    rm -f shader.vert_tvsimulator.h shader.frag_tvsimulator.h
    rm -f shader.vert_ios.h shader.frag_ios.h
    rm -f shader.vert_tvos.h shader.frag_tvos.h

elif [[ "$OSTYPE" == "cygwin"* ]] || [[ "$OSTYPE" == "msys"* ]]; then

    # HLSL
    spirv-cross shader.vert.spv --hlsl --shader-model 50 --output shader.vert.hlsl
    spirv-cross shader.frag.spv --hlsl --shader-model 50 --output shader.frag.hlsl

    # FXC
    # Assumes fxc is in the path.
    # If not, you can run `export PATH=$PATH:/c/Program\ Files\ \(x86\)/Windows\ Kits/10/bin/x.x.x.x/x64/`
    fxc shader.vert.hlsl /T vs_5_0 /Fh shader.vert.h
    fxc shader.frag.hlsl /T ps_5_0 /Fh shader.frag.h

    cat shader.vert.h | perl -w -p -e 's/BYTE/unsigned char/;s/main/vert_main/;' > cube_vert.h
    cat shader.frag.h | perl -w -p -e 's/BYTE/unsigned char/;s/main/frag_main/;' > cube_frag.h
    cat cube_vert.h cube_frag.h > testgpu_dxbc.h

fi

# cleanup
