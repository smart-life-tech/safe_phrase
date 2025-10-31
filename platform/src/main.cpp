#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== PSRAM Diagnostic ===");

  if (psramFound()) {
    Serial.println("✅ PSRAM detected and initialized successfully!");

    // Optional: show total and free PSRAM
    Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM:  %u bytes\n", ESP.getFreePsram());
    
    // Try to allocate 1MB in PSRAM
    uint8_t *psram_test = (uint8_t *)ps_malloc(1024 * 1024);
    if (psram_test) {
      Serial.println("✅ Successfully allocated 1MB from PSRAM!");
      free(psram_test);
    } else {
      Serial.println("⚠️ Failed to allocate 1MB from PSRAM (maybe fragmented or too small)");
    }
  } else {
    Serial.println("❌ PSRAM NOT detected!");
  }

  Serial.println("========================");
}

void loop() {
  // Nothing to do here
}
