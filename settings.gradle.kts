pluginManagement {
    repositories {
        maven {
            name = "FabricMC"
            url = uri("https://maven.fabricmc.net/")
        }
        maven {
            name = "AliyunPlugins"
            url = uri("https://maven.aliyun.com/repository/gradle-plugin/")
        }
        gradlePluginPortal()
    }
}

rootProject.name = "chineseime"
