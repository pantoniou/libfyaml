/*
 * libfyaml on Zephyr - parse a YAML document from an in-memory buffer.
 *
 * Demonstrates the embedded use case: the YAML never touches a filesystem.
 * Here it is a static string, but it could equally be a buffer filled from
 * BLE, Wi-Fi or a UART.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include <libfyaml.h>

/* A YAML config blob, as if just received over the air. */
static const char yaml[] =
	"device:\n"
	"  name: sensor-hub\n"
	"  id: 42\n"
	"  enabled: true\n"
	"network:\n"
	"  ssid: makerville\n"
	"  channels: [1, 6, 11]\n";

int main(void)
{
	struct fy_document *fyd;
	struct fy_node *root, *n;
	const char *name, *ssid;
	int id = -1;
	int rc;

	printf("libfyaml %s - parsing %zu bytes of YAML from RAM\n",
	       fy_library_version(), sizeof(yaml) - 1);

	/* Build a document tree directly from the in-memory string. */
	fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	if (!fyd) {
		printf("libfyaml: parse FAILED\n");
		return 1;
	}

	root = fy_document_root(fyd);

	/* Path lookups into the tree. */
	n = fy_node_by_path(root, "/device/name", FY_NT, FYNWF_DONT_FOLLOW);
	name = n ? fy_node_get_scalar0(n) : NULL;

	n = fy_node_by_path(root, "/network/ssid", FY_NT, FYNWF_DONT_FOLLOW);
	ssid = n ? fy_node_get_scalar0(n) : NULL;

	/* Typed extraction with a scanf-style helper. */
	rc = fy_document_scanf(fyd, "/device/id %d", &id);

	printf("  device.name  = %s\n", name ? name : "(null)");
	printf("  device.id    = %s (%d)\n", rc == 1 ? "ok" : "miss", id);
	printf("  network.ssid = %s\n", ssid ? ssid : "(null)");

	fy_document_destroy(fyd);

	if (name && ssid && rc == 1) {
		printf("libfyaml: parsed OK\n");
		return 0;
	}

	printf("libfyaml: parse incomplete\n");
	return 1;
}
