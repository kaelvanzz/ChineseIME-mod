plugins {
    id("fabric-loom") version "1.9.+"
    java
    id("maven-publish")
}

group = property("maven_group") as String
version = property("mod_version") as String

java {
    sourceCompatibility = JavaVersion.VERSION_21
    targetCompatibility = JavaVersion.VERSION_21
    withSourcesJar()
}

loom {
    // 1.21.4 官方映射
    accessWidenerPath.set(file("src/main/resources/chineseime.accesswidener"))
}

repositories {
    mavenCentral()
    maven("https://maven.aliyun.com/repository/public/") {
        name = "AliyunMirror"
    }
    maven("https://maven.fabricmc.net/")
    // Cloth Config (AutoConfig 依赖)
    maven("https://maven.shedaniel.me/")
    // ModMenu
    maven("https://maven.terraformersmc.com/releases/")
}

dependencies {
    // Minecraft
    minecraft("com.mojang:minecraft:${property("minecraft_version")}")
    
    // Yarn 映射
    mappings("net.fabricmc:yarn:${property("yarn_mappings")}:v2")
    
    // Fabric Loader
    modImplementation("net.fabricmc:fabric-loader:${property("loader_version")}")
    
    // Fabric API
    modImplementation("net.fabricmc.fabric-api:fabric-api:${property("fabric_version")}")
    
    // Cloth Config (配置 UI 库)
    modImplementation("me.shedaniel.cloth:cloth-config-fabric:15.0.140") {
        exclude(group = "net.fabricmc.fabric-api", module = "fabric-api")
    }
    
    // ModMenu (配置界面入口)
    modImplementation("com.terraformersmc:modmenu:12.0.0-beta.1")
    modApi("com.terraformersmc:modmenu:12.0.0-beta.1")
    
    // JNA (Java Native Access) - 用于调用Windows IMM32/TSF API
    implementation("net.java.dev.jna:jna:5.14.0")
    implementation("net.java.dev.jna:jna-platform:5.14.0")
}

tasks.processResources {
    inputs.properties("version" to version)

    filesMatching("fabric.mod.json") {
        expand("version" to version)
    }

    // 复制native DLL文件到JAR中 (按架构分类)
    from("natives/Release") {
        into("META-INF/natives/amd64")
        include("*.dll")
    }
}

tasks.withType<JavaCompile> {
    options.release.set(21)
}

// 发布配置
publishing {
    publications {
        create<MavenPublication>("mavenJava") {
            artifactId = property("archives_base_name") as String
            from(components["java"])
        }
    }
    
    repositories {
        mavenLocal()
    }
}
