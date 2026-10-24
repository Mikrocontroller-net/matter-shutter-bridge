#include "setup_code.h"

#include <stdio.h>

#include <bootloader_random.h>
#include <crypto/CHIPCryptoPAL.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <esp_random.h>
#include <lib/support/Span.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include <pairing/pairing.h>

#include "settings.h"

namespace setup_code {

static const char *TAG = "setup";

// Serves the stored code to the Matter stack in place of the test values.
class Provider : public chip::DeviceLayer::CommissionableDataProvider {
 public:
  pairing::Code code;

  CHIP_ERROR GetSetupDiscriminator(uint16_t &discriminator) override {
    discriminator = code.discriminator;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }
  CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterations) override {
    iterations = pairing::ITERATIONS;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &salt) override {
    return chip::CopySpanToMutableSpan(chip::ByteSpan(code.salt.data(), code.salt.size()), salt);
  }
  // Derived when a pairing window first opens, on the Matter task.
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &out, size_t &outLen) override {
    if (!haveVerifier_) {
      ReturnErrorOnFailure(verifier_.Generate(pairing::ITERATIONS, chip::ByteSpan(code.salt.data(), code.salt.size()),
                                              code.passcode));
      haveVerifier_ = true;
    }
    outLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
    return verifier_.Serialize(out);
  }
  CHIP_ERROR GetSetupPasscode(uint32_t &passcode) override {
    passcode = code.passcode;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

 private:
  chip::Crypto::Spake2pVerifier verifier_;
  bool haveVerifier_ = false;
};

static Provider provider;

void init() {
  if (!settings::loadSetupCode(provider.code)) {
    // Wi-Fi and Bluetooth are not running yet, so the hardware RNG needs this
    // entropy source to produce true random numbers.
    bootloader_random_enable();
    provider.code = pairing::generate([] { return esp_random(); });
    bootloader_random_disable();
    settings::saveSetupCode(provider.code);
    ESP_LOGI(TAG, "generated this device's setup code");
  }
  esp_matter::set_custom_commissionable_data_provider(&provider);
}

void print() {
  const chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE,
                                               chip::RendezvousInformationFlag::kOnNetwork);
  char qr[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
  char manual[chip::kManualSetupLongCodeCharLength + 1];
  char url[256];
  chip::MutableCharSpan qrSpan(qr);
  chip::MutableCharSpan manualSpan(manual);
  if (GetQRCode(qrSpan, flags) != CHIP_NO_ERROR || GetManualPairingCode(manualSpan, flags) != CHIP_NO_ERROR ||
      GetQRCodeUrl(url, sizeof(url), qrSpan) != CHIP_NO_ERROR) {
    ESP_LOGE(TAG, "cannot build the pairing codes");
    return;
  }
  // The 11-digit code as apps show it: 1234-567-8901.
  if (manualSpan.size() == 11) {
    printf("Pairing code: %.4s-%.3s-%.4s\n", manual, manual + 4, manual + 7);
  } else {
    printf("Pairing code: %s\n", manual);
  }
  printf("QR code:      %s\n", qr);
  printf("As image:     %s\n", url);
}

}  // namespace setup_code
