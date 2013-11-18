/*
	Copyright (C) 2013 CurlyMo

	This file is part of pilight.

    pilight is free software: you can redistribute it and/or modify it under the 
	terms of the GNU General Public License as published by the Free Software 
	Foundation, either version 3 of the License, or (at your option) any later 
	version.

    pilight is distributed in the hope that it will be useful, but WITHOUT ANY 
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR 
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <regex.h>
#include <sys/stat.h>

#include "config.h"
#include "common.h"
#include "log.h"
#include "options.h"
#include "protocol.h"

int config_update(char *protoname, JsonNode *json, JsonNode **out) {
	/* The pointer to the config locations */
	struct conf_locations_t *lptr = conf_locations;
	/* The pointer to the config devices */
	struct conf_devices_t *dptr = NULL;
	/* The pointer to the device settings */
	struct conf_settings_t *sptr = NULL;
	/* The pointer to the device settings */
	struct conf_values_t *vptr = NULL;	
	/* The pointer to the registered protocols */
	struct protocol_t *protocol = NULL;
	/* The pointer to the protocol options */
	struct options_t *opt = NULL;
	/* Get the code part of the sended code */
	JsonNode *code = json_find_member(json, "code");
	/* The return JSON object will all updated devices */
	JsonNode *rroot = json_mkobject();
	JsonNode *rdev = json_mkobject();
	JsonNode *rval = json_mkobject();

	/* Temporarily char pointer */
	char *stmp = NULL;
	/* Temporarily char array */
	char ctmp[255];
	/* Temporarily int */
	int itmp;
	/* Do we need to update the config file */
	unsigned short update = 0;
	/* The new state value */
	char state[255];

	/* Make sure the character poinrter are empty */
	memset(state, '\0', sizeof(state));
	memset(ctmp, '\0', sizeof(ctmp));

	/* Check if the found settings matches the send code */
	int match = 0;
	int match1 = 0;
	int match2 = 0;
	
	/* Was this device added to the return struct */
	int have_device = 0;
	
	/* Is is a valid new state / value */
	int is_valid = 1;

	/* Retrieve the used protocol */
	struct protocols_t *pnode = protocols;
	while(pnode) {
		protocol = pnode->listener;
		if(strcmp(protocol->id, protoname) == 0) {
			break;
		}
		pnode = pnode->next;
	}

	/* Only loop through all locations if the protocol has options */
	if((opt = protocol->options)) {

		/* Loop through all locations */
		while(lptr) {
			dptr = lptr->devices;
			/* Loop through all devices of this location */
			have_device = 0;

			JsonNode *rloc = NULL;
			while(dptr) {
				struct protocols_t *tmp_protocols = dptr->protocols;
				match = 0;
				while(tmp_protocols) {
					if(protocol_device_exists(protocol, tmp_protocols->name) == 0) {
						match = 1;
						break;
					}
					tmp_protocols = tmp_protocols->next;
				}
				if(match) {
					sptr = dptr->settings;
					/* Loop through all settings */
					while(sptr) {
						match1 = 0; match2 = 0;
						
						/* Check how many id's we need to match */
						opt = protocol->options;
						while(opt) {
							if(opt->conftype == config_id) {
								JsonNode *jtmp = json_first_child(code);
								while(jtmp) {
									if(strcmp(jtmp->key, opt->name) == 0) {
										match1++;
									}
									jtmp = jtmp->next;
								}
							}
							opt = opt->next;
						}

						/* Loop through all protocol options */
						opt = protocol->options;
						while(opt) {
							if(opt->conftype == config_id && strcmp(sptr->name, "id") == 0) {
								/* Check the config id's to match a device */								
								vptr = sptr->values;
								while(vptr) {
									if(strcmp(vptr->name, opt->name) == 0) {
										if(json_find_string(code, opt->name, &stmp) == 0) {
											strcpy(ctmp, stmp);
										}
										if(json_find_number(code, opt->name, &itmp) == 0) {
											sprintf(ctmp, "%d", itmp);
										}
										if(strcmp(ctmp, vptr->value) == 0) {
											match2++;
										}
									}
									vptr = vptr->next;
								}
							}
							/* Retrieve the new device state */
							if(opt->conftype == config_state && strlen(state) == 0) {
								if(opt->argtype == no_value) {
									if(json_find_string(code, "state", &stmp) == 0) {
										strcpy(state, stmp);
									}
									if(json_find_number(code, "state", &itmp) == 0) {
										sprintf(state, "%d", itmp);
									}
								} else if(opt->argtype == has_value) {
									if(json_find_string(code, opt->name, &stmp) == 0) {
										strcpy(state, stmp);
									}
									if(json_find_number(code, opt->name, &itmp) == 0) {
										sprintf(state, "%d", itmp);
									}
								}
							}
							opt = opt->next;
						}
						if(match1 > 0 && match2 > 0 && match1 == match2) {
							break;
						}
						sptr = sptr->next;
					}

					is_valid = 1;
					/* If we matched a config device, update it's state */
					if(match1 > 0 && match2 > 0 && match1 == match2) {
						if(protocol->checkValues) { 
							sptr = dptr->settings;
							/* Set possible protocol specific settings */
							while(sptr) {
								if(strcmp(sptr->name, "settings") == 0) {
									vptr = sptr->values;
									while(vptr) {
										if(vptr->type == CONFIG_TYPE_NUMBER) {
											protocol_setting_update_number(protocol, vptr->name, atoi(vptr->value));						
										} else if(vptr->type == CONFIG_TYPE_STRING) {
											protocol_setting_update_string(protocol, vptr->name, vptr->value);						
										}
										vptr = vptr->next;
									}
									break;
								}
								sptr = sptr->next;
							}
						}
						sptr = dptr->settings;
						while(sptr) {
							opt = protocol->options;
							/* Loop through all protocol options */
							while(opt) {
								/* Check if there are values that can be updated */
								if(strcmp(sptr->name, opt->name) == 0 && opt->conftype == config_value && opt->argtype == has_value) {

									memset(ctmp, '\0', sizeof(ctmp));
									if(json_find_string(code, opt->name, &stmp) == 0) {
										strcpy(ctmp, stmp);
									}

									if(json_find_number(code, opt->name, &itmp) == 0) {
										sprintf(ctmp, "%d", itmp);
									}

									/* Check if the protocol settings of this device are valid to 
									   make sure no errors occur in the config.json. */
									if(protocol->checkValues) {
										JsonNode *jcode = json_mkobject();
										json_append_member(jcode, opt->name, json_mkstring(ctmp));
										if(protocol->checkValues(jcode) != 0) {
											is_valid = 0;
											json_delete(jcode);
											break;
										} else {
											json_delete(jcode);
										}
									}

									if(strlen(ctmp) > 0 && is_valid) {
										if(strcmp(sptr->values->value, ctmp) != 0) {
											sptr->values->value = realloc(sptr->values->value, strlen(ctmp)+1);
											strcpy(sptr->values->value, ctmp);
										}

										if(json_find_string(rval, sptr->name, &stmp) != 0) {
											json_append_member(rval, sptr->name, json_mkstring(sptr->values->value));
											update = 1;
										}

										if(have_device == 0) {
											if(rloc == NULL) {
												rloc = json_mkarray();
											}

											json_append_element(rloc, json_mkstring(dptr->id));
											have_device = 1;
										}
									}
									//break;
								}
								opt = opt->next;
							}

							/* Check if we need to update the state */
							if(strcmp(sptr->name, "state") == 0) {
								if(strcmp(sptr->values->value, state) != 0) {
									sptr->values->value = realloc(sptr->values->value, strlen(state)+1);
									strcpy(sptr->values->value, state);
									update = 1;
								}
								
								if(json_find_string(rval, sptr->name, &stmp) != 0) {
									json_append_member(rval, sptr->name, json_mkstring(sptr->values->value));
								}
								if(rloc == NULL) {
									rloc = json_mkarray();
								}
								json_append_element(rloc, json_mkstring(dptr->id));
								have_device = 1;
								//break;
							}
							sptr = sptr->next;
						}
						/* Restore the protocol settings to it's default value */
						if(protocol->checkValues) { 
							sptr = dptr->settings;
							while(sptr) {
								if(strcmp(sptr->name, "settings") == 0) {
									vptr = sptr->values;
									while(vptr) {
										protocol_setting_restore(protocol, vptr->name);
										vptr = vptr->next;
									}
									break;
								}
								sptr = sptr->next;
							}
						}						
					}
				}
				dptr = dptr->next;
			}
			if(have_device == 1) {
				json_append_member(rdev, lptr->id, rloc);
			}
			lptr = lptr->next;
		}
	}
	json_append_member(rroot, "origin", json_mkstring("config"));
	json_append_member(rroot, "type",  json_mknumber((int)protocol->devtype));
	json_append_member(rroot, "devices", rdev);
	json_append_member(rroot, "values", rval);

	/* Only update the config file, if a state change occured */
	if(update == 1) {
		// if(configfile) {
			// JsonNode *joutput = config2json(0);
			// char *output = json_stringify(joutput, "\t");
			// config_write(output);
			// json_delete(joutput);
			// sfree((void *)&output);			
			// joutput = NULL;
		// }
		*out = rroot;
	} else {
		json_delete(rroot);
	}

	return (update == 1) ? 0 : -1;
}

int config_get_location(char *id, struct conf_locations_t **loc) {
	struct conf_locations_t *lptr = conf_locations;
	while(lptr) {
		if(strcmp(lptr->id, id) == 0) {
			*loc = lptr;
			return 0;
		}
		lptr = lptr->next;
	}
	return 1;
}

int config_get_device(char *lid, char *sid, struct conf_devices_t **dev) {
	struct conf_locations_t *lptr = NULL;
	struct conf_devices_t *dptr = NULL;

	if(config_get_location(lid, &lptr) == 0) {
		while(lptr) {
			dptr = lptr->devices;
			while(dptr) {
				if(strcmp(dptr->id, sid) == 0) {
					*dev = dptr;
					return 0;
				}
				dptr = dptr->next;
			}
			lptr = lptr->next;
		}
	}
	return 1;
}

int config_valid_state(char *lid, char *sid, char *state) {
	struct conf_devices_t *dptr = NULL;
	struct protocol_t *protocol = NULL;
	struct options_t *options = NULL;
	struct protocols_t *tmp_protocol = NULL;
	
	if(config_get_device(lid, sid, &dptr) == 0) {
		tmp_protocol = dptr->protocols;
		while(tmp_protocol) {
			protocol = tmp_protocol->listener;
			if(protocol->options) {
				options = protocol->options;
				while(options) {
					if(strcmp(options->name, state) == 0 && options->conftype == config_state) {
						return 0;
						break;
					}
					options = options->next;
				}
			}
			tmp_protocol = tmp_protocol->next;
		}
	}
	return 1;
}

int config_valid_value(char *lid, char *sid, char *name, char *value) {
	struct conf_devices_t *dptr = NULL;
	struct options_t *opt = NULL;
	struct protocols_t *tmp_protocol = NULL;
#ifndef __FreeBSD__	
	regex_t regex;
	int reti;
#endif
	
	if(config_get_device(lid, sid, &dptr) == 0) {
		tmp_protocol = dptr->protocols;
		while(tmp_protocol) {
			opt = tmp_protocol->listener->options;
			while(opt) {
				if(opt->conftype == config_value && strcmp(name, opt->name) == 0) {
#ifndef __FreeBSD__				
					reti = regcomp(&regex, opt->mask, REG_EXTENDED);
					if(reti) {
						logprintf(LOG_ERR, "could not compile regex");
						exit(EXIT_FAILURE);
					}
					reti = regexec(&regex, value, 0, NULL, 0);
					if(reti == REG_NOMATCH || reti != 0) {
						regfree(&regex);
						return 1;
					}
					regfree(&regex);
#endif
					return 0;
				}
				opt = opt->next;
			}
			tmp_protocol = tmp_protocol->next;
		}
	}
	return 1;
}

/* http://stackoverflow.com/a/13654646 */
void config_reverse_struct(struct conf_locations_t **loc) {
    if(!loc || !*loc)
        return;

    struct conf_locations_t *lptr = *loc, *lnext = NULL, *lprev = NULL;
    struct conf_devices_t *dptr = NULL, *dnext = NULL, *dprev = NULL;
    struct conf_settings_t *sptr = NULL, *snext = NULL, *sprev = NULL;
    struct conf_values_t *vptr = NULL, *vnext = NULL, *vprev = NULL;
    struct protocols_t *pptr = NULL, *pnext = NULL, *pprev = NULL;

    while(lptr) {

		dptr = lptr->devices;
		while(dptr) {

			sptr = dptr->settings;
			while(sptr) {

				vptr = sptr->values;
				while(vptr) {
					vnext = vptr->next;
					vptr->next = vprev;
					vprev = vptr;
					vptr = vnext;
				}
				sptr->values = vprev;
				vptr = NULL;
				vprev = NULL;
				vnext = NULL;

				snext = sptr->next;
				sptr->next = sprev;
				sprev = sptr;
				sptr = snext;
			}
			dptr->settings = sprev;
			sptr = NULL;
			sprev = NULL;
			snext = NULL;

			pptr = dptr->protocols;
			while(pptr) {
				pnext = pptr->next;
				pptr->next = pprev;
				pprev = pptr;
				pptr = pnext;
			}
			dptr->protocols = pprev;
			pptr = NULL;
			pprev = NULL;
			pnext = NULL;
			
			dnext = dptr->next;
			dptr->next = dprev;
			dprev = dptr;
			dptr = dnext;
		}
		lptr->devices = dprev;
		dptr = NULL;
		dprev = NULL;
		dnext = NULL;

        lnext = lptr->next;
        lptr->next = lprev;
        lprev = lptr;
        lptr = lnext;
    }
	sfree((void *)&lptr);
	sfree((void *)&dptr);
	sfree((void *)&sptr);
	sfree((void *)&vptr);
	sfree((void *)&lnext);
	sfree((void *)&dnext);
	sfree((void *)&snext);
	sfree((void *)&vnext);

    *loc = lprev;	
}

JsonNode *config2json(unsigned short internal) {
	/* Temporary pointer to the different structure */
	struct conf_locations_t *tmp_locations = NULL;
	struct conf_devices_t *tmp_devices = NULL;
	struct conf_settings_t *tmp_settings = NULL;
	struct conf_values_t *tmp_values = NULL;

	/* Pointers to the newly created JSON object */
	struct JsonNode *jroot = json_mkobject();
	struct JsonNode *jlocation = NULL;
	struct JsonNode *jdevice = NULL;
	struct JsonNode *joptions = NULL;
	struct JsonNode *jid = NULL;
	struct JsonNode *jsettings = NULL;
	struct protocol_settings_t *psettings = NULL;

	int lorder = 0;
	int dorder = 0;
	int has_settings = 0;

	/* Make sure we preserve the order of the original file */
	tmp_locations = conf_locations;

	/* Show the parsed config file */
	while(tmp_locations) {
		lorder++;
		jlocation = json_mkobject();
		json_append_member(jlocation, "name", json_mkstring(tmp_locations->name));
		if(internal) {
			json_append_member(jlocation, "order", json_mknumber(lorder));
		}

		dorder = 0;
		tmp_devices = tmp_locations->devices;

		while(tmp_devices) {
			dorder++;
			jdevice = json_mkobject();
			json_append_member(jdevice, "name", json_mkstring(tmp_devices->name));
			if(internal) {
				json_append_member(jdevice, "order", json_mknumber(dorder));
			}
			struct protocols_t *tmp_protocols = tmp_devices->protocols;
			struct JsonNode *jprotocols = json_mkarray();
			if(internal) {
				json_append_member(jdevice, "type", json_mknumber(tmp_protocols->listener->devtype));
			}			
			while(tmp_protocols) {
				json_append_element(jprotocols, json_mkstring(tmp_protocols->name));
				tmp_protocols = tmp_protocols->next;
			}
			json_append_member(jdevice, "protocol", jprotocols);
			json_append_member(jdevice, "id", json_mkarray());

			has_settings = 0;
			tmp_settings = tmp_devices->settings;
			jsettings = json_mkobject();
			while(tmp_settings) {
				tmp_values = tmp_settings->values;
				if(strcmp(tmp_settings->name, "id") == 0) {
					jid = json_find_member(jdevice, "id");
					JsonNode *jnid = json_mkobject();
					while(tmp_values) {
						if(tmp_values->type == CONFIG_TYPE_NUMBER) {
							json_append_member(jnid, tmp_values->name, json_mknumber(atoi(tmp_values->value)));
						} else if(tmp_values->type == CONFIG_TYPE_STRING) {
							json_append_member(jnid, tmp_values->name, json_mkstring(tmp_values->value));
						}
						tmp_values = tmp_values->next;
					}
					json_append_element(jid, jnid);
					sfree((void *)&tmp_values);
				} else if(strcmp(tmp_settings->name, "settings") == 0) {
					has_settings = 1;
					while(tmp_values) {
						if(tmp_values->type == CONFIG_TYPE_NUMBER) {
							json_append_member(jsettings, tmp_values->name, json_mknumber(atoi(tmp_values->value)));
						} else if(tmp_values->type == CONFIG_TYPE_STRING) {
							json_append_member(jsettings, tmp_values->name, json_mkstring(tmp_values->value));
						}
						tmp_values = tmp_values->next;
					}
					sfree((void *)&tmp_values);
				} else if(!tmp_values->next) {
					if(tmp_values->type == CONFIG_TYPE_NUMBER) {
						json_append_member(jdevice, tmp_settings->name, json_mknumber(atoi(tmp_values->value)));
					} else if(tmp_values->type == CONFIG_TYPE_STRING) {
						json_append_member(jdevice, tmp_settings->name, json_mkstring(tmp_values->value));
					}
				} else {
					joptions = json_mkarray();
					while(tmp_values) {
						if(tmp_values->type == CONFIG_TYPE_NUMBER) {
							json_append_element(joptions, json_mknumber(atoi(tmp_values->value)));
						} else if(tmp_values->type == CONFIG_TYPE_STRING) {
							json_append_element(joptions, json_mkstring(tmp_values->value));
						}
						tmp_values = tmp_values->next;
					}
					sfree((void *)&tmp_values);
					json_append_member(jdevice, tmp_settings->name, joptions);
				}
				tmp_settings = tmp_settings->next;
			}

			tmp_protocols = tmp_devices->protocols;
			while(tmp_protocols) {
				psettings = tmp_protocols->listener->settings;
				if(psettings) {
					while(psettings) {
						if(internal && (json_find_member(jsettings, psettings->name) == NULL)) {
							has_settings = 1;
							if(psettings->type == 1) {
								json_append_member(jsettings, psettings->name, json_mkstring(psettings->cur_value));
							} else {
								json_append_member(jsettings, psettings->name, json_mknumber(atoi(psettings->cur_value)));
							}
						}
						psettings = psettings->next;
					}
				}
				tmp_protocols = tmp_protocols->next;
			}
			if(has_settings == 0) {
				json_delete(jsettings);
			} else {
				json_append_member(jdevice, "settings", jsettings);
			}
			sfree((void *)&tmp_settings);			

			json_append_member(jlocation, tmp_devices->id, jdevice);
			tmp_devices = tmp_devices->next;
		}
		sfree((void *)&tmp_devices);
		json_append_member(jroot, tmp_locations->id, jlocation);
		tmp_locations = tmp_locations->next;
	}

	sfree((void *)&tmp_locations);

	return jroot;
}

void config_print(void) {
	logprintf(LOG_DEBUG, "-- start parsed config file --");
	JsonNode *joutput = config2json(0);
	char *output = json_stringify(joutput, "\t");
	printf("%s\n", output);
	json_delete(joutput);
	sfree((void *)&output);
	joutput = NULL;
	logprintf(LOG_DEBUG, "-- end parsed config file --");
}

/* If a fault was found in the config file, clear everything */
void config_clear(void) {
	conf_values = NULL;
	conf_settings = NULL;
	conf_devices = NULL;
	conf_locations = NULL;
}

/* Save the device settings to the device struct */
void config_save_setting(int i, JsonNode *jsetting, struct conf_settings_t *snode) {
	/* Struct to store the values */
	struct conf_values_t *vnode = NULL;
	/* Temporary JSON pointer */
	struct JsonNode *jtmp;

	/* Variable holder for casting settings */
	char *stmp = NULL;
	char ctmp[256];
	int itmp = 0;

	/* If the JSON tag is an array, then it should be a values or id array */
	if(jsetting->tag == JSON_ARRAY) {
		if(strcmp(jsetting->key, "id") == 0) {
			sfree((void *)&snode);
			
			/* Loop through the values of this values array */
			jtmp = json_first_child(jsetting);
			while(jtmp) {
				snode = malloc(sizeof(struct conf_settings_t));
				snode->name = malloc(strlen(jsetting->key)+1);
				strcpy(snode->name, jsetting->key);			
				if(jtmp->tag == JSON_OBJECT) {
					JsonNode *jtmp1 = json_first_child(jtmp);	
					while(jtmp1) {
						vnode = malloc(sizeof(struct conf_values_t));			
						vnode->name = malloc(strlen(jtmp1->key)+1);
						strcpy(vnode->name, jtmp1->key);
						if(jtmp1->tag == JSON_STRING) {
							vnode->value = malloc(strlen(jtmp1->string_)+1);
							strcpy(vnode->value, jtmp1->string_);
							vnode->type = CONFIG_TYPE_STRING;
							vnode->next = conf_values;
							conf_values = vnode;	
						} else if(jtmp1->tag == JSON_NUMBER) {
							sprintf(ctmp, "%d", (int)jtmp1->number_);
							vnode->value = malloc(strlen(ctmp)+1);
							strcpy(vnode->value, ctmp);
							vnode->type = CONFIG_TYPE_NUMBER;
							vnode->next = conf_values;
							conf_values = vnode;	
						}
						jtmp1 = jtmp1->next;	
					}
					json_delete(jtmp1);			
				}
				jtmp = jtmp->next;
				
				snode->values = malloc(sizeof(struct conf_values_t));
				/* Only store values if they are present */
				if(conf_values) {
					memcpy(snode->values, conf_values, (sizeof(struct conf_values_t)));
				} else {
					snode->values = NULL;
				}
				snode->next = conf_settings;
				conf_settings = snode;

				/* Make sure to clean all pointer so values don't end up in subsequent structure */
				conf_values = NULL;
				if(vnode && vnode->next)
					vnode->next = NULL;
				
				sfree((void *)&vnode);
			}
		}
	} else if(jsetting->tag == JSON_OBJECT) {
		snode->name = malloc(strlen(jsetting->key)+1);
		strcpy(snode->name, jsetting->key);

		jtmp = json_first_child(jsetting);
		while(jtmp) {
			if(jtmp->tag == JSON_STRING) {
				vnode = malloc(sizeof(struct conf_values_t));			
				vnode->name = malloc(strlen(jtmp->key)+1);
				strcpy(vnode->name, jtmp->key);			
				vnode->value = malloc(strlen(jtmp->string_)+1);
				strcpy(vnode->value, jtmp->string_);
				vnode->type = CONFIG_TYPE_STRING;
				vnode->next = conf_values;
				conf_values = vnode;	
			} else if(jtmp->tag == JSON_NUMBER) {
				vnode = malloc(sizeof(struct conf_values_t));			
				vnode->name = malloc(strlen(jtmp->key)+1);
				strcpy(vnode->name, jtmp->key);			
				sprintf(ctmp, "%d", (int)jtmp->number_);
				vnode->value = malloc(strlen(ctmp)+1);
				strcpy(vnode->value, ctmp);
				vnode->type = CONFIG_TYPE_NUMBER;
				vnode->next = conf_values;
				conf_values = vnode;	
			}
			jtmp = jtmp->next;
		}
		snode->values = malloc(sizeof(struct conf_values_t));
		/* Only store values if they are present */
		if(conf_values) {
			memcpy(snode->values, conf_values, (sizeof(struct conf_values_t)));
		} else {
			snode->values = NULL;
		}
		snode->next = conf_settings;
		conf_settings = snode;

		/* Make sure to clean all pointer so values don't end up in subsequent structure */
		conf_values = NULL;
		if(vnode && vnode->next)
			vnode->next = NULL;
		
		sfree((void *)&vnode);		
	} else {
		/* New device settings node */
		snode->name = malloc(strlen(jsetting->key)+1);
		strcpy(snode->name, jsetting->key);
	
		vnode = malloc(sizeof(struct conf_values_t));

		/* Cast and store the new value */
		if(jsetting->tag == JSON_STRING && json_find_string(jsetting->parent, jsetting->key, &stmp) == 0) {
			vnode->value = malloc(strlen(stmp)+1);
			vnode->name = malloc(4);
			strcpy(vnode->value, stmp);
			vnode->type = CONFIG_TYPE_STRING;
		} else if(jsetting->tag == JSON_NUMBER && json_find_number(jsetting->parent, jsetting->key, &itmp) == 0) {
			sprintf(ctmp, "%d", itmp);
			vnode->value = malloc(strlen(ctmp)+1);
			vnode->name = malloc(4);
			strcpy(vnode->value, ctmp);
			vnode->type = CONFIG_TYPE_NUMBER;
		}
		vnode->next = conf_values;
		conf_values = vnode;

		snode->values = malloc(sizeof(struct conf_values_t));
		/* Only store values if they are present */
		if(conf_values) {
			memcpy(snode->values, conf_values, (sizeof(struct conf_values_t)));
		} else {
			snode->values = NULL;
		}
		snode->next = conf_settings;
		conf_settings = snode;

		/* Make sure to clean all pointer so values don't end up in subsequent structure */
		conf_values = NULL;
		if(vnode && vnode->next)
			vnode->next = NULL;
		
		sfree((void *)&vnode);
	}
}

int config_check_id(int i, JsonNode *jsetting, struct conf_devices_t *device) {
	/* Temporary options pointer */
	struct options_t *tmp_options = NULL;
	/* Temporary ID array pointer */
	struct JsonNode *jid = NULL;
	/* Temporary ID values pointer */	
	struct JsonNode *jvalues = NULL;
	/* Temporary protocols pointer */
	struct protocols_t *tmp_protocols = NULL;

	int match1 = 0, match2 = 0, has_id = 0;
	int valid_values = 0, nrvalues = 0, nrids1 = 0, nrids2 = 0, have_error = 0;

	/* Variable holders for casting */
	char ctmp[256];

	tmp_protocols = device->protocols;
	while(tmp_protocols) {
		jid = json_first_child(jsetting);
		has_id = 0;
		while(jid) {
			match2 = 0; match1 = 0; nrids1 = 0; nrids2 = 0;
			jvalues = json_first_child(jid);
			while(jvalues) {
				nrids1++;
				jvalues = jvalues->next;
			}
			if((tmp_options = tmp_protocols->listener->options)) {
				while(tmp_options) {
					if(tmp_options->conftype == config_id) {
						nrids2++;
					}
					tmp_options = tmp_options->next;
				}
			}
			if(nrids1 == nrids2) {
				has_id = 1;
				nrvalues++;
				jvalues = json_first_child(jid);
				while(jvalues) {
					match1++;
					if((tmp_options = tmp_protocols->listener->options)) {
						while(tmp_options) {
							if(tmp_options->conftype == config_id) {
								if(strcmp(tmp_options->name, jvalues->key) == 0) {
									match2++;
									if(jvalues->tag == JSON_NUMBER) {
										sprintf(ctmp, "%d", (int)jvalues->number_);
									} else if(jvalues->tag == JSON_STRING) {
										strcpy(ctmp, jvalues->string_);
									}
									
									if(strlen(tmp_options->mask) > 0) {
										regex_t regex;
										int reti = regcomp(&regex, tmp_options->mask, REG_EXTENDED);
										if(reti) {
											logprintf(LOG_ERR, "could not compile regex");
										} else {
											reti = regexec(&regex, ctmp, 0, NULL, 0);
											if(reti == REG_NOMATCH || reti != 0) {
												match2--;
											}
											regfree(&regex);
										}
									}
								}
							}
							tmp_options = tmp_options->next;
						}
					}
					jvalues = jvalues->next;
				}
			}
			json_delete(jvalues);
			jid = jid->next;
			if(match2 > 0) {
				if(match1 == match2) {
					valid_values++;
				} else {
					valid_values--;
				}
			}
		}
		if(!has_id) {
			valid_values--;
		}
		json_delete(jid);
		tmp_protocols = tmp_protocols->next;
	}
	if(nrvalues != valid_values) {
		logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", invalid", i, "id", device->id);
		have_error = 1;
	}	
	return have_error;
}

int config_check_settings(int i, JsonNode *jsetting, struct conf_devices_t *device) {
	JsonNode *jtmp = json_first_child(jsetting);
	int valid_setting = 0, have_error = 0;

	while(jtmp) {
		valid_setting = 0;
		struct protocols_t *tmp_protocols = device->protocols;
		while(tmp_protocols) {
			if(jtmp->tag == JSON_STRING) {
				if(protocol_setting_check_string(tmp_protocols->listener, jtmp->key, jtmp->string_) == 0) {	
					valid_setting=1;
				}
			} else if(jtmp->tag == JSON_NUMBER) {
				if(protocol_setting_check_number(tmp_protocols->listener, jtmp->key, (int)jtmp->number_) == 0) {
					valid_setting=1;
				}
			}
			tmp_protocols = tmp_protocols->next;
		}
		if(valid_setting == 0) {
			have_error = 1;
			break;
		}
		jtmp = jtmp->next;
	}
	json_delete(jtmp);
	if(have_error) {
		logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", invalid", i, "settings", device->id);
	}
	return have_error;
}

int config_validate_settings(void) {
	/* Temporary pointer to the different structure */
	struct conf_locations_t *tmp_locations = NULL;
	struct conf_devices_t *tmp_devices = NULL;
	struct conf_settings_t *tmp_settings = NULL;
	struct conf_values_t *tmp_values = NULL;

	/* Pointers to the newly created JSON object */
	struct JsonNode *jdevice = NULL;
	struct JsonNode *joptions = NULL;

	int have_error = 0;
	int dorder = 0;
	/* Make sure we preserve the order of the original file */
	tmp_locations = conf_locations;

	/* Show the parsed config file */
	while(tmp_locations) {

		tmp_devices = tmp_locations->devices;
		dorder = 0;
		while(tmp_devices) {

			jdevice = json_mkobject();
			struct protocols_t *tmp_protocols = tmp_devices->protocols;
			while(tmp_protocols) {
				/* Only continue if protocol specific settings can be validated */
				if(tmp_protocols->listener->checkValues) {
					tmp_settings = tmp_devices->settings;
					dorder++;
					while(tmp_settings) {
						tmp_values = tmp_settings->values;
						/* Retrieve all protocol specific settings for this device. Also add all
						   device values and states so it can be validated by the protocol */
						if(strcmp(tmp_settings->name, "settings") == 0) {
							while(tmp_values) {
								if(tmp_values->type == CONFIG_TYPE_NUMBER) {
									if(protocol_setting_check_number(tmp_protocols->listener, tmp_values->name, atoi(tmp_values->value)) == 0) {
										protocol_setting_update_number(tmp_protocols->listener, tmp_values->name, atoi(tmp_values->value));						
									}
								} else if(tmp_values->type == CONFIG_TYPE_STRING) {
									if(protocol_setting_check_string(tmp_protocols->listener, tmp_values->name, tmp_values->value) == 0) {
										protocol_setting_update_string(tmp_protocols->listener, tmp_values->name, tmp_values->value);						
									}
								}
								tmp_values = tmp_values->next;
							}
							sfree((void *)&tmp_values);
						} else if(strcmp(tmp_settings->name, "id") != 0) {
							if(!tmp_values->next) {
								json_append_member(jdevice, tmp_settings->name, json_mkstring(tmp_values->value));
							} else {
								joptions = json_mkarray();
								while(tmp_values) {
									json_append_element(joptions, json_mkstring(tmp_values->value));
									tmp_values = tmp_values->next;
								}
								sfree((void *)&tmp_values);
								json_append_member(jdevice, tmp_settings->name, joptions);
							}
						}
						tmp_settings = tmp_settings->next;
					}

					sfree((void *)&tmp_settings);
					/* Let the settings and values be validated against each other */
					if(tmp_protocols->listener->checkValues(jdevice) != 0) {
						logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", invalid", dorder, tmp_devices->name, tmp_locations->name);
						have_error = 1;
						goto clear;
					}

					/* Restore all protocol specific settings to their default values */
					tmp_settings = tmp_devices->settings;			
					while(tmp_settings) {
						tmp_values = tmp_settings->values;
						if(strcmp(tmp_settings->name, "settings") == 0) {
							while(tmp_values) {
								if(tmp_values->type == CONFIG_TYPE_NUMBER) {
									if(protocol_setting_check_number(tmp_protocols->listener, tmp_values->name, atoi(tmp_values->value)) == 0) {
										protocol_setting_restore(tmp_protocols->listener, tmp_values->name);
									}
								} else if(tmp_values->type == CONFIG_TYPE_STRING) {
									if(protocol_setting_check_string(tmp_protocols->listener, tmp_values->name, tmp_values->value) == 0) {
										protocol_setting_restore(tmp_protocols->listener, tmp_values->name);
									}
								}							
								tmp_values = tmp_values->next;
							}
							sfree((void *)&tmp_values);
						}
						tmp_settings = tmp_settings->next;
					}
				}
				tmp_protocols = tmp_protocols->next;
			}
			tmp_devices = tmp_devices->next;
			json_delete(jdevice);
			jdevice=NULL;
		}
		sfree((void *)&tmp_devices);
		tmp_locations = tmp_locations->next;
	}

	sfree((void *)&tmp_locations);	
	
clear:
	if(jdevice) {
		json_delete(jdevice);
	}
	return have_error;
}

int config_check_state(int i, JsonNode *jsetting, struct conf_devices_t *device) {
	/* Temporary options pointer */
	struct options_t *tmp_options;

	int valid_state = 0, have_error = 0;

	/* Variable holders for casting */
	int itmp = 0;
	char ctmp[256];
	char *stmp = NULL;

	/* Regex variables */
	regex_t regex;
	int reti;

	/* Cast the different values */
	if(jsetting->tag == JSON_NUMBER && json_find_number(jsetting->parent, jsetting->key, &itmp) == 0) {
		sprintf(ctmp, "%d", itmp);
	} else if(jsetting->tag == JSON_STRING && json_find_string(jsetting->parent, jsetting->key, &stmp) == 0) {
		strcpy(ctmp, stmp);
	}

	struct protocols_t *tmp_protocols = device->protocols;
	while(tmp_protocols) {
		/* Check if the specific device contains any options */
		if(tmp_protocols->listener->options) {

			tmp_options = tmp_protocols->listener->options;

			while(tmp_options) {
				/* We are only interested in the config_* options */
				if(tmp_options->conftype == config_state) {
					/* If an option requires an argument, then check if the
					   argument state values and values array are of the right
					   type. This is done by checking the regex mask */
					if(tmp_options->argtype == has_value) {
						if(strlen(tmp_options->mask) > 0) {
							reti = regcomp(&regex, tmp_options->mask, REG_EXTENDED);
							if(reti) {
								logprintf(LOG_ERR, "could not compile regex");
								have_error = 1;
								goto clear;
							}
							reti = regexec(&regex, ctmp, 0, NULL, 0);

							if(reti == REG_NOMATCH || reti != 0) {
								logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", invalid", i, jsetting->key, device->id);
								have_error = 1;
								regfree(&regex);
								goto clear;
							}
							regfree(&regex);
						}
					} else {
						/* If a protocol has config_state arguments, than these define
						   the states a protocol can take. Check if the state value
						   match, these protocol states */
						
						if(strcmp(tmp_options->name, ctmp) == 0 && valid_state == 0) {
							valid_state = 1;
						}
					}
				}
				tmp_options = tmp_options->next;
			}
		}
		tmp_protocols = tmp_protocols->next;
	}

	if(valid_state == 0) {
		logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", invalid", i, jsetting->key, device->id);
		have_error = 1;
		goto clear;
	}
	
	sfree((void *)&tmp_options);

clear:
	return have_error;
}

int config_parse_devices(JsonNode *jdevices, struct conf_devices_t *device) {
	/* Struct to store the settings */
	struct conf_settings_t *snode = NULL;
	/* Temporary settings holder */
	struct conf_settings_t *tmp_settings = NULL;
	/* JSON devices iterator */
	JsonNode *jsettings = NULL;
	/* Temporarily options pointer */
	struct options_t *tmp_options = NULL;
	/* Temporarily protocols pointer */
	struct protocols_t *tmp_protocols = NULL;

	int i = 0, have_error = 0, valid_setting = 0, match = 0, has_state = 0;
	/* Check for any duplicate fields */
	int nrname = 0, nrprotocol = 0, nrstate = 0, nrorder = 0, nrsettings = 0;

	jsettings = json_first_child(jdevices);
	while(jsettings) {
		i++;
		/* Check for any duplicate fields */
		if(strcmp(jsettings->key, "name") == 0) {
			nrname++;
		}
		if(strcmp(jsettings->key, "order") == 0) {
			nrorder++;
		}		
		if(strcmp(jsettings->key, "protocol") == 0) {
			nrprotocol++;
		}
		if(strcmp(jsettings->key, "state") == 0) {
			nrstate++;
		}
		if(strcmp(jsettings->key, "settings") == 0) {
			nrsettings++;
		}
		if(nrstate > 1 || nrprotocol > 1 || nrname > 1 || nrorder > 1 || nrsettings > 1) {
			logprintf(LOG_ERR, "settting #%d \"%s\" of \"%s\", duplicate", i, jsettings->key, device->id);
			have_error = 1;
			goto clear;
		}
		
		/* Parse the state setting separately from the other settings. */
		if(strcmp(jsettings->key, "state") == 0 && (jsettings->tag == JSON_STRING || jsettings->tag == JSON_NUMBER)) {
			if(config_check_state(i, jsettings, device) != 0) {
				have_error = 1;
				goto clear;
			}
			snode = malloc(sizeof(struct conf_settings_t));
			config_save_setting(i, jsettings, snode);
		} else if(strcmp(jsettings->key, "id") == 0 && jsettings->tag == JSON_ARRAY) {
			if(config_check_id(i, jsettings, device) == EXIT_FAILURE) {
				have_error = 1;
				goto clear;
			}	
			snode = malloc(sizeof(struct conf_settings_t));
			config_save_setting(i, jsettings, snode);
		} else if(strcmp(jsettings->key, "settings") == 0 && jsettings->tag == JSON_OBJECT) {
			if(config_check_settings(i, jsettings, device) != 0) {
				have_error = 1;
				goto clear;
			}
			snode = malloc(sizeof(struct conf_settings_t));
			config_save_setting(i, jsettings, snode);
		
		/* The protocol and name settings are already saved in the device struct */
		} else if(!((strcmp(jsettings->key, "name") == 0 && jsettings->tag == JSON_STRING)
			|| (strcmp(jsettings->key, "protocol") == 0 && jsettings->tag == JSON_ARRAY)
			|| (strcmp(jsettings->key, "type") == 0 && jsettings->tag == JSON_NUMBER)
			|| (strcmp(jsettings->key, "order") == 0 && jsettings->tag == JSON_NUMBER))) {

			/* Check for duplicate settings */
			tmp_settings = conf_settings;
			while(tmp_settings) {
				if(strcmp(tmp_settings->name, jsettings->key) == 0) {
					logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", duplicate", i, jsettings->key, device->id);
					have_error = 1;
					goto clear;
				}
				tmp_settings = tmp_settings->next;
			}

			tmp_protocols = device->protocols;
			valid_setting = 0;			
			while(tmp_protocols) {
				/* Check if the settings are required by the protocol */			
				if(tmp_protocols->listener->options) {
					tmp_options = tmp_protocols->listener->options;
					while(tmp_options) {
						if(strcmp(jsettings->key, tmp_options->name) == 0 && (tmp_options->conftype == config_id || tmp_options->conftype == config_value)) {
							valid_setting = 1;
							break;
						}
						tmp_options = tmp_options->next;
					}
				}
				tmp_protocols = tmp_protocols->next;
			}
			/* If the settings are valid, store them */
			if(valid_setting == 1) {
				snode = malloc(sizeof(struct conf_settings_t));
				config_save_setting(i, jsettings, snode);
			} else {
				logprintf(LOG_ERR, "setting #%d \"%s\" of \"%s\", invalid", i, jsettings->key, device->id);
				have_error = 1;
				goto clear;
			}
		}
		jsettings = jsettings->next;
	}

	/* Check if required options by this protocol are missing
	   in the config file */
	tmp_protocols = device->protocols;
	while(tmp_protocols) {
		if(tmp_protocols->listener->options) {
			tmp_options = tmp_protocols->listener->options;
			while(tmp_options) {
				match = 0;
				jsettings = json_first_child(jdevices);
				while(jsettings) {
					if(strcmp(jsettings->key, tmp_options->name) == 0) {
						match = 1;
						break;
					}
					jsettings = jsettings->next;
				}
				if(match == 0 && tmp_options->conftype == config_value) {
					logprintf(LOG_ERR, "setting \"%s\" of \"%s\", missing", tmp_options->name, device->id);
					have_error = 1;
					goto clear;
				}
			tmp_options = tmp_options->next;
			}
			
		}
		tmp_protocols = tmp_protocols->next;
	}
	match = 0;
	jsettings = json_first_child(jdevices);
	while(jsettings) {
		if(strcmp(jsettings->key, "id") == 0) {
			match = 1;
			break;
		}
		jsettings = jsettings->next;
	}
	if(match == 0) {
		logprintf(LOG_ERR, "setting \"id\" of \"%s\", missing", device->id);
		have_error = 1;
		goto clear;
	}

	/* Check if this protocol requires a state setting */
	if(nrstate == 0) {
		tmp_protocols = device->protocols;
		while(tmp_protocols) {
			if(tmp_protocols->listener->options) {
				tmp_options = tmp_protocols->listener->options;
				while(tmp_options) {
					if(tmp_options->conftype == config_state) {
						has_state = 1;
						break;
					}
					tmp_options = tmp_options->next;
				}
			}
			if(has_state == 1) {
				if(nrstate == 0) {
					logprintf(LOG_ERR, "setting \"%s\" of \"%s\", missing", "state", device->id);
					have_error = 1;
					goto clear;
				}
			}
			tmp_protocols = tmp_protocols->next;
		}
	}

	device->settings = malloc(sizeof(struct conf_settings_t));
	/* Only store devices if they are present */
	if(conf_settings) {
		memcpy(device->settings, conf_settings, (sizeof(struct conf_settings_t)));
	} else {
		device->settings = NULL;
	}

	/* Clear the locations struct for the next location */
	conf_settings = NULL;

	if(snode) {
		if(snode->next) {
			snode->next = NULL;
		}
		sfree((void *)&snode);
	}
	
	sfree((void *)&tmp_settings);
	sfree((void *)&tmp_options);
clear:
	return have_error;
}

int config_parse_locations(JsonNode *jlocations, struct conf_locations_t *location) {
	/* Struct to store the devices */
	struct conf_devices_t *dnode = NULL;
	/* Temporary JSON devices  */
	struct conf_devices_t *tmp_devices = NULL;
	/* Temporary protocol JSON array */
	JsonNode *jprotocol = NULL;
	JsonNode *jprotocols = NULL;
	/* JSON devices iterator */
	JsonNode *jdevices = NULL;
	/* Device name */
	char *name = NULL;

	int i = 0, have_error = 0, match = 0;
	/* Check for duplicate name setting */
	int nrname = 0;
	/* Check for duplicate order setting */
	int nrorder = 0;

	jdevices = json_first_child(jlocations);
	while(jdevices) {
		i++;

		/* Check for duplicate name setting */
		if(strcmp(jdevices->key, "name") == 0) {
			nrname++;
			if(nrname > 1) {
				logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", duplicate", i, jdevices->key, location->id);
				have_error = 1;
				goto clear;
			}
		}
		/* Check for duplicate name setting */
		if(strcmp(jdevices->key, "order") == 0) {
			nrorder++;
			if(nrorder > 1) {
				logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", duplicate", i, jdevices->key, location->id);
				have_error = 1;
				goto clear;
			}
		}		
		/* Check if all fields of the devices are of the right type */
		if(!((strcmp(jdevices->key, "name") == 0 && jdevices->tag == JSON_STRING) || (strcmp(jdevices->key, "order") == 0 && jdevices->tag == JSON_NUMBER) || (jdevices->tag == JSON_OBJECT))) {
			logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", invalid field(s)", i, jdevices->key, location->id);
			have_error = 1;
			goto clear;
		} else if(jdevices->tag == JSON_OBJECT) {
			/* Save the name of the device */
			if(json_find_string(jdevices, "name", &name) != 0) {
				logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", missing name", i, jdevices->key, location->id);
				have_error = 1;
				goto clear;
			/* Save the protocol of the device */
			} else if((jprotocols = json_find_member(jdevices, "protocol")) == NULL && jprotocols->tag == JSON_ARRAY) {
				logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", missing protocol", i, jdevices->key, location->id);
				have_error = 1;
				goto clear;
			} else {
				/* Check for duplicate fields */
				tmp_devices = conf_devices;
				while(tmp_devices) {
					if(strcmp(tmp_devices->id, jdevices->key) == 0) {
						logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", duplicate", i, jdevices->key, location->id);
						have_error = 1;
						goto clear;
					}
					tmp_devices = tmp_devices->next;
				}

				dnode = malloc(sizeof(struct conf_devices_t));
				dnode->id = malloc(strlen(jdevices->key)+1);
				dnode->protocols = NULL;
				strcpy(dnode->id, jdevices->key);
				dnode->name = malloc(strlen(name)+1);
				strcpy(dnode->name, name);

				//int ptype = -1;
				/* Save both the protocol pointer and the protocol name */
				jprotocol = json_first_child(jprotocols);
				while(jprotocol) {
					match = 0;
					struct protocols_t *tmp_protocols = protocols;
					/* Pointer to the match protocol */
					protocol_t *protocol = NULL;					
					while(tmp_protocols) {
						protocol = tmp_protocols->listener;
						if(protocol_device_exists(protocol, jprotocol->string_) == 0 && match == 0) {
							// if(ptype == -1) {
								// ptype = protocol->type;
								// match = 1;
							// }
							// if(ptype > -1 && protocol->type == ptype) {
								// match = 1;
							// }
							match = 1;
							break;
						}
						tmp_protocols = tmp_protocols->next;
					}
					// if(ptype != protocol->type) {
						// logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", cannot combine protocols of different types", i, jdevices->key, location->id);
						// have_error = 1;
						// json_delete(jprotocol);
						// sfree((void *)&dnode);
						// goto clear;
					// }
					if(match == 0) {
						logprintf(LOG_ERR, "device #%d \"%s\" of \"%s\", invalid protocol", i, jdevices->key, location->id);
						have_error = 1;
						json_delete(jprotocol);
						sfree((void *)&dnode);
						goto clear;
					} else {
						struct protocols_t *pnode = malloc(sizeof(struct protocols_t));
						pnode->listener = malloc(sizeof(struct protocol_t));
						memcpy(pnode->listener, protocol, sizeof(struct protocol_t));
						pnode->name = malloc(strlen(jprotocol->string_)+1);
						strcpy(pnode->name, jprotocol->string_);
						pnode->next = dnode->protocols;
						dnode->protocols = pnode;
					}
					jprotocol = jprotocol->next;
				}
				json_delete(jprotocol);
				dnode->next = conf_devices;

				if(config_parse_devices(jdevices, dnode) != 0) {
					have_error = 1;
					goto clear;
				}
				conf_devices = dnode;
			}
		}

		jdevices = jdevices->next;
	}

	location->devices = malloc(sizeof(struct conf_devices_t));
	/* Only store devices if they are present */
	if(conf_devices) {
		memcpy(location->devices, conf_devices, (sizeof(struct conf_devices_t)));
	} else {
		location->devices = NULL;
	}
	/* Clear the locations struct for the next location */
	conf_devices = NULL;

	sfree((void *)&dnode);
	sfree((void *)&tmp_devices);
clear:
	return have_error;
}

int config_parse(JsonNode *root) {
	/* Struct to store the locations */
	struct conf_locations_t *lnode = NULL;
	struct conf_locations_t *tmp_locations = NULL;
	/* JSON locations iterator */
	JsonNode *jlocations = NULL;
	/* Location name */
	char *name = NULL;

	int i = 0, have_error = 0;

	jlocations = json_first_child(root);

	while(jlocations) {
		i++;
		/* An location can only be a JSON object */
		if(jlocations->tag != 5) {
			logprintf(LOG_ERR, "location #%d \"%s\", invalid format", i, jlocations->key);
			have_error = 1;
			goto clear;
		/* Check if the location has a name */
		} else if(json_find_string(jlocations, "name", &name) != 0) {
			logprintf(LOG_ERR, "location #%d \"%s\", missing name", i, jlocations->key);
			have_error = 1;
			goto clear;
		} else {
			/* Check for duplicate locations */
			tmp_locations = conf_locations;
			while(tmp_locations) {
				if(strcmp(tmp_locations->id, jlocations->key) == 0) {
					logprintf(LOG_ERR, "location #%d \"%s\", duplicate", i, jlocations->key);
					have_error = 1;
					goto clear;
				}
				tmp_locations = tmp_locations->next;
			}

			lnode = malloc(sizeof(struct conf_locations_t));
			lnode->id = malloc(strlen(jlocations->key)+1);
			strcpy(lnode->id, jlocations->key);
			lnode->name = malloc(strlen(name)+1);
			strcpy(lnode->name, name);
			lnode->next = conf_locations;

			if(config_parse_locations(jlocations, lnode) != 0) {
				have_error = 1;
				goto clear;
			}

			conf_locations = lnode;

			jlocations = jlocations->next;
		}
	}

	/* Preverse the original order inside the structs as in the config file */
	config_reverse_struct(&conf_locations);

	sfree((void *)&tmp_locations);
	json_delete(jlocations);
clear:
	return have_error;
}

int config_write(char *content) {
	FILE *fp;

	if(access(configfile, F_OK) != -1) {
		/* Overwrite config file with proper format */
		if(!(fp = fopen(configfile, "w+"))) {
			logprintf(LOG_ERR, "cannot write config file: %s", configfile);
			return EXIT_FAILURE;
		}
		if(strcmp(content, "{}") == 0) {
			return EXIT_SUCCESS;
		}
		fseek(fp, 0L, SEEK_SET);
		fwrite(content, sizeof(char), strlen(content), fp);
		fclose(fp);
	} else {
		logprintf(LOG_ERR, "the config file %s does not exists\n", configfile);
	}

	return EXIT_SUCCESS;
}

int config_gc(void) {
	sfree((void *)&configfile);
	
	struct conf_locations_t *ltmp;
	struct conf_devices_t *dtmp;
	struct conf_settings_t *stmp;
	struct conf_values_t *vtmp;
	struct protocols_t *ptmp;
	/* Show the parsed log file */
	while(conf_locations) {
		ltmp = conf_locations;
		while(ltmp->devices) {
			dtmp = ltmp->devices;
			while(dtmp->settings) {
				stmp = dtmp->settings;
				while(stmp->values) {
					vtmp = stmp->values;
					sfree((void *)&vtmp->value);
					sfree((void *)&vtmp->name);
					stmp->values = stmp->values->next;
					sfree((void *)&vtmp);
				}
				sfree((void *)&stmp->values);
				sfree((void *)&stmp->name);
				dtmp->settings = dtmp->settings->next;
				sfree((void *)&stmp);
			}
			while(dtmp->protocols) {
				ptmp = dtmp->protocols;
				sfree((void *)&ptmp->name);
				sfree((void *)&ptmp->listener);
				dtmp->protocols = dtmp->protocols->next;
				sfree((void *)&ptmp);
			}
			sfree((void *)&dtmp->protocols);
			sfree((void *)&dtmp->settings);
			sfree((void *)&dtmp->id);
			sfree((void *)&dtmp->name);
			ltmp->devices = ltmp->devices->next;
			sfree((void *)&dtmp);
		}
		sfree((void *)&ltmp->devices);		
		sfree((void *)&ltmp->id);
		sfree((void *)&ltmp->name);
		conf_locations = conf_locations->next;
		sfree((void *)&ltmp);
	}
	sfree((void *)&conf_locations);
	
	logprintf(LOG_DEBUG, "garbage collected config library");
	
	return EXIT_SUCCESS;
}

int config_read() {
	FILE *fp;
	char *content;
	size_t bytes;
	JsonNode *root;
	struct stat st;

	/* Read JSON config file */
	if(!(fp = fopen(configfile, "rb"))) {
		logprintf(LOG_ERR, "cannot read config file: %s", configfile);
		return EXIT_FAILURE;
	}

	fstat(fileno(fp), &st);
	bytes = (size_t)st.st_size;

	if(!(content = calloc(bytes+1, sizeof(char)))) {
		logprintf(LOG_ERR, "out of memory");
		return EXIT_FAILURE;
	}

	if(fread(content, sizeof(char), bytes, fp) == -1) {
		logprintf(LOG_ERR, "cannot read config file: %s", configfile);
	}
	fclose(fp);

	/* Validate JSON and turn into JSON object */
	if(json_validate(content) == false) {
		logprintf(LOG_ERR, "config is not in a valid json format", content);
		sfree((void *)&content);
		return EXIT_FAILURE;
	}
	root = json_decode(content);

	sfree((void *)&content);

	if(config_parse(root) == 0 && config_validate_settings() == 0) {
		JsonNode *joutput = config2json(0);
		char *output = json_stringify(joutput, "\t");
		config_write(output);
		json_delete(joutput);
		sfree((void *)&output);			
		joutput = NULL;
		json_delete(root);
		root = NULL;
		return EXIT_SUCCESS;
	} else {
		json_delete(root);
		root = NULL;
		return EXIT_FAILURE;
	}
}

int config_set_file(char *cfgfile) {
	if(access(cfgfile, F_OK) != -1) {
		configfile = realloc(configfile, strlen(cfgfile)+1);
		strcpy(configfile, cfgfile);
	} else {
		logprintf(LOG_ERR, "the config file %s does not exists\n", cfgfile);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
