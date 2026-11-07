plugins {
    id("com.android.application")
}

// The desktop sample and the Android demo share one page implementation.
val generatedDemoAssets = layout.buildDirectory.dir("generated/demoAssets").get().asFile
val copyDemoAssets = tasks.register<Copy>("copyDemoAssets") {
    from(rootProject.projectDir.resolve("../demo/resources"))
    into(File(generatedDemoAssets, "demo"))
}

android {
    namespace = "io.github.systemwebview.demo"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    sourceSets.getByName("main").assets.directories.add(generatedDemoAssets.absolutePath)

    defaultConfig {
        applicationId = "io.github.systemwebview.demo"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
        // The emulator milestone targets x86_64. Add "arm64-v8a" for devices.
        ndk {
            abiFilters += listOf("x86_64")
        }
        externalNativeBuild {
            cmake {
                // Two shared libraries exchange C++ objects, so they must share one STL.
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
        }
    }
}

dependencies {
    implementation("androidx.webkit:webkit:1.18.0")
}

tasks.named("preBuild").configure {
    dependsOn(copyDemoAssets)
}

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }.configureEach {
    dependsOn(copyDemoAssets)
}
