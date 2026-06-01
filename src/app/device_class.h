#ifndef DEVICE_CLASS_H
#define DEVICE_CLASS_H

// Device Class registry: lets a "specialized device built on top of the core
// firmware" (e.g. an e-paper dashboard, a coffee scale, an LED strip) plug
// into core lifecycle, power, config, and MQTT paths without the core code
// having to know it exists.
//
// A device class is a struct of optional function-pointer hooks. The core
// firmware calls `device_class_dispatch_*` helpers at well-defined points;
// each helper iterates the registered classes and invokes any non-null hook.
// All hooks are optional — a class only fills in what it needs.
//
// Registration is a single call (typically from an aggregator like
// `device_classes.cpp` that pulls in each class implementation under its
// HAS_* feature gate). The registry is a fixed-size static array, so there
// is no heap allocation and no order-of-init dependency on global ctors.

#include "power_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

struct DeviceConfig;
class MqttManager;

// Hard cap on registered device classes. A handful covers any realistic
// firmware; raising the cap is cheap (static array + 1 counter byte).
#ifndef MAX_DEVICE_CLASSES
#define MAX_DEVICE_CLASSES 4
#endif

struct DeviceClass {
		// Human-readable identifier, used only for logging.
		const char *name;

		// Power mode this class "owns". When the boot mode matches, the core
		// will route duty-cycle execution to `run_duty_cycle`. Use
		// PowerMode::AlwaysOn (the default) for classes that don't own a mode.
		PowerMode owned_mode;

		// Lifecycle ------------------------------------------------------------
		// Called early in setup() after config load and power-mode resolution,
		// before any network/portal init.
		void (*on_setup_early)(DeviceConfig *config, PowerMode boot_mode);
		// Called late in setup() after WiFi / AP / portal init.
		void (*on_setup_late)(DeviceConfig *config, PowerMode current_mode);
		// Called every iteration of loop().
		void (*on_loop)();

		// Duty cycle -----------------------------------------------------------
		// Owns the entire duty cycle for `owned_mode`. Returns true on success.
		bool (*run_duty_cycle)(DeviceConfig *config);

		// Power ----------------------------------------------------------------
		// Called from power_manager_boot_init(). If the class consumed the
		// wake reason (e.g. classified a wake-button press), it sets
		// *handled = true. May also set *force_config = true to push the
		// device into Config Mode.
		void (*on_wake_classify)(bool *handled, bool *force_config);
		// Called from power_manager_sleep_for() right before deep-sleep is
		// armed. May mutate *seconds_inout (e.g. zero it for a button-only
		// sleep) and arm additional wake sources (ext0/ext1/etc.) directly.
		void (*on_sleep_prepare)(uint32_t *seconds_inout);

		// Config ---------------------------------------------------------------
		// Called when no valid config exists yet, to populate device-class
		// fields with sensible defaults.
		void (*config_defaults)(DeviceConfig *config);
		// Called during load with the NVS namespace already open (read-only).
		void (*config_load)(DeviceConfig *config, Preferences &preferences);
		// Called during save with the NVS namespace already open (read-write).
		void (*config_save)(const DeviceConfig *config, Preferences &preferences);
		// Append device-class fields to the GET /api/config response.
		void (*config_api_get)(const DeviceConfig *config, JsonObject &root);
		// Parse device-class fields from the POST /api/config body into the
		// in-memory config struct (caller persists via config_manager_save).
		void (*config_api_set)(DeviceConfig *config, JsonObject &body);

		// MQTT -----------------------------------------------------------------
		// Publish HA discovery for this device class. If the class wants the
		// core to skip its own generic discovery (e.g. because the class
		// publishes a customized subset on a slow / battery-powered link),
		// it sets *skip_generic = true.
		void (*mqtt_on_discovery)(MqttManager &mqtt, bool *skip_generic);
		// Publish an immediate device-class state snapshot when the core needs
		// one (currently during discovery bootstrap; may also be reused by
		// always-on/periodic paths). Duty-cycle classes typically publish from
		// inside `run_duty_cycle` using their own helpers instead of this hook.
		void (*mqtt_publish_state)(MqttManager &mqtt);

		// Pad engine-hold (optional) -------------------------------------------
		// A class that drives a hardware engine (e.g. an ADC) needed only while
		// a pad consuming its binding scheme is on screen declares the scheme's
		// token prefix here (e.g. "[shutter:"). The generic pad screen scans
		// each visible pad's bindings; when the prefix is present it calls
		// `pad_hold_acquire` on show and `pad_hold_release` on hide/removal.
		// Both are reference-counted by the class and receive a debug holder
		// tag. A NULL `pad_hold_scheme` (or NULL hooks) disables the mechanism.
		// This keeps device-class hardware lifetime out of the core screen code.
		const char *pad_hold_scheme;
		bool (*pad_hold_acquire)(const char *holder);
		void (*pad_hold_release)(const char *holder);
};

// Registration ----------------------------------------------------------------
// Returns true if the class was accepted, false if the registry is full.
bool device_class_register(const DeviceClass *cls);
// Number of currently registered classes.
unsigned device_class_count();
// Access a registered class by index (nullptr if out of range).
const DeviceClass *device_class_get(unsigned index);
// Lookup the class that owns `mode`, or nullptr if none.
const DeviceClass *device_class_find_by_mode(PowerMode mode);

// Dispatch helpers ------------------------------------------------------------
// Lifecycle
void device_class_dispatch_setup_early(DeviceConfig *config, PowerMode boot_mode);
void device_class_dispatch_setup_late(DeviceConfig *config, PowerMode current_mode);
void device_class_dispatch_loop();
// Power
bool device_class_dispatch_wake_classify(bool *force_config);
void device_class_dispatch_sleep_prepare(uint32_t *seconds_inout);
// Config
void device_class_dispatch_config_defaults(DeviceConfig *config);
void device_class_dispatch_config_load(DeviceConfig *config, Preferences &preferences);
void device_class_dispatch_config_save(const DeviceConfig *config, Preferences &preferences);
void device_class_dispatch_config_api_get(const DeviceConfig *config, JsonObject &root);
void device_class_dispatch_config_api_set(DeviceConfig *config, JsonObject &body);
// MQTT
void device_class_dispatch_mqtt_discovery(MqttManager &mqtt, bool *skip_generic);
void device_class_dispatch_mqtt_state(MqttManager &mqtt);

#endif // DEVICE_CLASS_H
