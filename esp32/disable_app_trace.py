Import("env")

# Remove 'app_trace' component before IDF builds
try:
    if "app_trace" in env['BUILD_COMPONENTS']:
        env['BUILD_COMPONENTS'].remove("app_trace")
        print("✅ Disabled ESP-IDF app_trace component")
    else:
        print("ℹ️ app_trace component not found, nothing to disable")
except Exception as e:
    print(f"⚠️ Could not remove app_trace: {e}")
