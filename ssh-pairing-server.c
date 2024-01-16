// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE

#include <libssh/libssh.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <libssh/callbacks.h>
#include <libssh/server.h>

int main(int argc, char *argv[])
{
	ssh_bind bind = ssh_bind_new();

	// If a file doesn't exist it's ignored. If no host keys are available,
	// ssh_bind_listen will fail immediately with a helpful error.
	ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, "/etc/ssh/ssh_host_ecdsa_key");
	ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, "/etc/ssh/ssh_host_ed25519_key");
	ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, "/etc/ssh/ssh_host_rsa_key");

	if (ssh_bind_listen(bind) < 0) {
		fprintf(stderr, "Failed to listen: %s\n", ssh_get_error(bind));
		return 1;
	}

	ssh_session session = ssh_new();

	if (ssh_bind_accept(bind, session) != SSH_OK) {
		fprintf(stderr, "Failed to accept: %s\n", ssh_get_error(bind));
		return 1;
	}

	if (ssh_handle_key_exchange(session)) {
		fprintf(stderr, "Key exchange failed: %s\n", ssh_get_error(session));
		return 1;
	}

	// Handle messages until all pubkey auth requests are done and either
	// the client moves on to keyboard-interactive or disconnects due to no
	// further methods. The use of keyboard-interactive means that unfortunately
	// the callback-based pubkey handling is not possible:
	// https://gitlab.com/libssh/libssh-mirror/-/issues/146

	ssh_set_auth_methods(session, SSH_AUTH_METHOD_PUBLICKEY | SSH_AUTH_METHOD_INTERACTIVE);

	// TODO: Ask user for key name?
	// TODO: Key list in interactive prompt is truncated
	// TODO: Ask user which key to import?

	char *authorized_keys = strdup("");

	ssh_message message;
	while((message = ssh_message_get(session))) {
		int msg_type = ssh_message_type(message),
		    msg_subtype = ssh_message_subtype(message);

		if(msg_type == SSH_REQUEST_AUTH && msg_subtype == SSH_AUTH_METHOD_PUBLICKEY) {
			ssh_key pubkey = ssh_message_auth_pubkey(message);
			char *key_fp;
			if(ssh_pki_export_pubkey_base64(pubkey, &key_fp) == 0) {
				const char *key_type = ssh_key_type_to_char(ssh_key_type(pubkey));
				char *new_authorized_keys;
				if(asprintf(&new_authorized_keys, "%s%s %s %s@pairing\n", authorized_keys, key_type, key_fp, ssh_message_auth_user(message)) > 0) {
					free(authorized_keys);
					authorized_keys = new_authorized_keys;
				}
				free(key_fp);
			}
		} else if(msg_type == SSH_REQUEST_AUTH && msg_subtype == SSH_AUTH_METHOD_INTERACTIVE) {
			ssh_message_auth_interactive_request(message, "Received public keys:",
									authorized_keys, 0, NULL, 0);
			ssh_message_free(message);
			break;
		}

		ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PUBLICKEY | SSH_AUTH_METHOD_INTERACTIVE);
		ssh_message_reply_default(message);

        ssh_message_free(message);
    }

	printf("%s", authorized_keys);

	ssh_disconnect(session);
    ssh_bind_free(bind);
    ssh_finalize();
}