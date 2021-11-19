/*
** Copyright (C) 2018-2021 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2018-2021 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* Read in Meer configuration/YAML file */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#ifdef HAVE_LIBYAML
#include <yaml.h>
#endif

#ifndef HAVE_LIBYAML
** You must of LIBYAML installed! **
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "meer.h"
#include "meer-def.h"
#include "config-yaml.h"
#include "util.h"
#include "decode-json-alert.h"

#ifdef WITH_BLUEDOT
#include "output-plugins/bluedot.h"
struct _Bluedot_Skip *Bluedot_Skip = NULL;
#endif

#ifdef WITH_ELASTICSEARCH
#include "output-plugins/elasticsearch.h"
#endif

#include "output-plugins/external.h"

extern struct _MeerConfig *MeerConfig;
extern struct _MeerOutput *MeerOutput;
extern struct _MeerCounters *MeerCounters;

struct _MeerHealth *MeerHealth = NULL;
struct _Fingerprint_Networks *Fingerprint_Networks = NULL;

void Load_YAML_Config( char *yaml_file )
{

    struct stat filecheck;

    yaml_parser_t parser;
    yaml_event_t  event;

    bool done = 0;

    unsigned char type = 0;
    unsigned char sub_type = 0;

    char last_pass[128] = { 0 };

    char *ptr1 = NULL;
    char *ptr2 = NULL;
    char tmp[512] = { 0 };

    bool routing = false;

    /* For SQL "health checks" */

    MeerHealth = (struct _MeerHealth *) malloc(sizeof(_MeerHealth));

    if ( MeerHealth == NULL )
        {
            Meer_Log(ERROR, "[%s, line %d] Failed to allocate memory for _MeerHealth. Abort!", __FILE__, __LINE__);
        }

    memset(MeerHealth, 0, sizeof(_MeerHealth));

    /* Init MeerConfig values */

    MeerConfig->fingerprint = false;

    strlcpy(MeerConfig->meer_log, MEER_LOG, sizeof(MeerConfig->meer_log));
    strlcpy(MeerConfig->description, MEER_DESC, sizeof( MeerConfig->description ));
    MeerConfig->payload_buffer_size = PACKET_BUFFER_SIZE_DEFAULT;

    MeerOutput = (struct _MeerOutput *) malloc(sizeof(_MeerOutput));

    if ( MeerOutput == NULL )
        {
            Meer_Log(ERROR, "[%s, line %d] Failed to allocate memory for _MeerOutput. Abort!", __FILE__, __LINE__);
        }

    memset(MeerOutput, 0, sizeof(_MeerOutput));

#if defined(HAVE_LIBMYSQLCLIENT) || defined(HAVE_LIBPQ)

    MeerOutput->sql_enabled = false;
    MeerOutput->sql_debug = false;
    MeerOutput->sql_extra_data = true;
    MeerOutput->sql_reconnect = true;
    MeerOutput->sql_reconnect_time = SQL_RECONNECT_TIME;

#endif

#ifdef HAVE_LIBHIREDIS

    MeerOutput->redis_enabled = false;
    MeerOutput->redis_port = 6379;
    MeerOutput->redis_batch = 1;

    strlcpy(MeerOutput->redis_server, "127.0.0.1", sizeof(MeerOutput->redis_server));
    strlcpy(MeerOutput->redis_command, "set", sizeof(MeerOutput->redis_command));

#endif

#ifdef WITH_ELASTICSEARCH

    MeerOutput->elasticsearch_batch = 10;
    MeerOutput->elasticsearch_threads = 5;

#endif

    MeerConfig->client_stats = false;
    MeerConfig->oui = false;

    MeerOutput->pipe_size =  DEFAULT_PIPE_SIZE;

    if (stat(yaml_file, &filecheck) != false )
        {
            Meer_Log(ERROR, "[%s, line %d] Cannot open configuration file '%s'! %s", __FILE__, __LINE__, yaml_file, strerror(errno) );
        }

    FILE *fh = fopen(yaml_file, "r");

    if (!yaml_parser_initialize(&parser))
        {
            Meer_Log(ERROR, "[%s, line %d] Failed to initialize the libyaml parser. Abort!", __FILE__, __LINE__);
        }

    if (fh == NULL)
        {
            Meer_Log(ERROR, "[%s, line %d] Failed to open the configuration file '%s' Abort!", __FILE__, __LINE__, yaml_file);
        }

    /* Set input file */

    yaml_parser_set_input_file(&parser, fh);

    while(!done)
        {

            if (!yaml_parser_parse(&parser, &event))
                {

                    /* Useful YAML vars: parser.context_mark.line+1, parser.context_mark.column+1, parser.problem, parser.problem_mark.line+1, parser.problem_mark.column+1 */

                    Meer_Log(ERROR, "[%s, line %d] libyam parse error at line %d in '%s'", __FILE__, __LINE__, parser.problem_mark.line+1, yaml_file);
                }

            if ( event.type == YAML_DOCUMENT_START_EVENT )
                {

                    yaml_version_directive_t *ver = event.data.document_start.version_directive;

                    if ( ver == NULL )
                        {
                            Meer_Log(ERROR, "[%s, line %d] Invalid configuration file. Configuration must start with \"%%YAML 1.1\"", __FILE__, __LINE__);
                        }

                    int major = ver->major;
                    int minor = ver->minor;

                    if (! (major == YAML_VERSION_MAJOR && minor == YAML_VERSION_MINOR) )
                        {
                            Meer_Log(ERROR, "[%s, line %d] Configuration has a invalid YAML version.  Must be 1.1 or above", __FILE__, __LINE__);
                        }

                }

            else if ( event.type == YAML_STREAM_END_EVENT )
                {

                    done = true;

                }

            else if ( event.type == YAML_MAPPING_END_EVENT )
                {

                    sub_type = 0;

                }

            else if ( event.type == YAML_SCALAR_EVENT )
                {

                    char *value = (char *)event.data.scalar.value;

                    if ( !strcmp(value, "meer-core"))
                        {
                            type = YAML_TYPE_MEER;
                        }

                    else if ( !strcmp(value, "output-plugins"))
                        {
                            type = YAML_TYPE_OUTPUT;
                        }


                    if ( type == YAML_TYPE_MEER )
                        {

                            if ( !strcmp(value, "core") )
                                {
                                    sub_type = YAML_MEER_CORE_CORE;
                                }

                        }

                    else if ( type == YAML_TYPE_OUTPUT )
                        {
                            if ( !strcmp(value, "sql") )
                                {
                                    sub_type = YAML_MEER_SQL;
                                    routing == false;
                                }

                            if ( !strcmp(value, "pipe") )
                                {
                                    sub_type = YAML_MEER_PIPE;
                                    routing == false;
                                }

                            if ( !strcmp(value, "external") )
                                {
                                    sub_type = YAML_MEER_EXTERNAL;
                                    routing == false;
                                }

                            if ( !strcmp(value, "redis") )
                                {
                                    sub_type = YAML_MEER_REDIS;
                                    routing == false;
                                }

                            if ( !strcmp(value, "file") )
                                {
                                    sub_type = YAML_MEER_FILE;
                                    routing == false;
                                }

#ifdef WITH_BLUEDOT

                            if ( !strcmp(value, "bluedot") )
                                {
                                    sub_type = YAML_MEER_BLUEDOT;
                                    routing == false;
                                }

#endif

                            if ( !strcmp(value, "elasticsearch") )
                                {
                                    sub_type = YAML_MEER_ELASTICSEARCH;
                                    routing == false;
                                }

                        }

                    if ( type == YAML_TYPE_MEER && sub_type == YAML_MEER_CORE_CORE )
                        {

                            if ( !strcmp(last_pass, "interface" ))
                                {
                                    strlcpy(MeerConfig->interface, value, sizeof(MeerConfig->interface));
                                }

                            if ( !strcmp(last_pass, "description" ))
                                {
                                    strlcpy(MeerConfig->description, value, sizeof(MeerConfig->description));
                                }

                            else if ( !strcmp(last_pass, "hostname" ))
                                {
                                    strlcpy(MeerConfig->hostname, value, sizeof(MeerConfig->hostname));
                                }

                            else if ( !strcmp(last_pass, "runas" ))
                                {
                                    strlcpy(MeerConfig->runas, value, sizeof(MeerConfig->runas));
                                }

                            else if ( !strcmp(last_pass, "classification" ))
                                {
                                    strlcpy(MeerConfig->classification_file, value, sizeof(MeerConfig->classification_file));
                                }

                            else if ( !strcmp(last_pass, "waldo-file" ) || !strcmp(last_pass, "waldo_file" ) )
                                {
                                    strlcpy(MeerConfig->waldo_file, value, sizeof(MeerConfig->waldo_file));
                                }

                            else if ( !strcmp(last_pass, "lock-file" ) || !strcmp(last_pass, "lock_file" ) )
                                {
                                    strlcpy(MeerConfig->lock_file, value, sizeof(MeerConfig->lock_file));
                                }

                            else if ( !strcmp(last_pass, "meer_log" ))
                                {
                                    strlcpy(MeerConfig->meer_log, value, sizeof(MeerConfig->meer_log));
                                }

                            else if ( !strcmp(last_pass, "follow-eve" ) || !strcmp(last_pass, "follow_eve" ) )
                                {
                                    strlcpy(MeerConfig->follow_file, value, sizeof(MeerConfig->follow_file));
                                }

                            else if ( !strcmp(last_pass, "payload-buffer-size" ) )
                                {


                                    if ( ( value[ strlen(value) - 2 ] != 'k' || value[ strlen(value) - 1 ] != 'b' ) &&
                                            ( value[ strlen(value) - 2 ] != 'm' || value[ strlen(value) - 1 ] != 'b' ) &&
                                            ( value[ strlen(value) - 2 ] != 'g' || value[ strlen(value) - 1 ] != 'b' ) )
                                        {
                                            Meer_Log(ERROR, "[%s, line %d] The 'payload-buffer-size' has an invalid size.  It needs to be kb, mb or gb.", __FILE__, __LINE__);
                                        }

                                    strlcpy(tmp, value, sizeof(value));
                                    tmp[ strlen(tmp) - 2 ] = '\0';		/* Remove kb, mb, gb */

                                    if ( value[ strlen(value) - 2 ] == 'k' && value[ strlen(value) - 1 ] == 'b' )
                                        {
                                            MeerConfig->payload_buffer_size = atoi(tmp) * 1024;
                                        }

                                    else if ( value[ strlen(value) - 2 ] == 'm' && value[ strlen(value) - 1 ] == 'b' )
                                        {
                                            MeerConfig->payload_buffer_size = atoi(tmp) * 1024 * 1024;
                                        }

                                    else if ( value[ strlen(value) - 2 ] == 'g' && value[ strlen(value) - 1 ] == 'b' )
                                        {
                                            MeerConfig->payload_buffer_size = atoi(tmp) * 1024 * 1024 * 1024;
                                        }

                                }

                            else if ( !strcmp(last_pass, "dns" ))
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->dns = true;
                                            MeerConfig->dns_cache = DNS_CACHE_DEFAULT;
                                        }

                                }

                            else if ( !strcmp(last_pass, "dns_cache" ))
                                {

                                    MeerConfig->dns_cache = atoi(value);

                                }

#ifdef HAVE_LIBMAXMINDDB

                            else if ( !strcmp(last_pass, "geoip" ))
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->geoip = true;
                                        }

                                }

                            else if ( !strcmp(last_pass, "geoip_database" ) && MeerConfig->geoip == true )
                                {
                                    strlcpy(MeerConfig->geoip_database, value, sizeof(MeerConfig->geoip_database));
                                }


#endif

                            else if ( !strcmp(last_pass, "oui_lookup" ))
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->oui = true;
                                        }

                                }

                            else if ( !strcmp(last_pass, "oui_filename" ))
                                {
                                    strlcpy(MeerConfig->oui_filename, value, sizeof(MeerConfig->oui_filename));
                                }

                            else if ( !strcmp(last_pass, "fingerprint" ) )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->fingerprint = true;
                                        }

                                }

                            else if ( !strcmp(last_pass, "fingerprint_networks" )  && MeerConfig->fingerprint == true )
                                {

                                    char *fp_ptr = NULL;
                                    char *fp_range = NULL;
                                    char *tok = NULL;
                                    char *fp_ipblock = NULL;

                                    unsigned char fp_ipbits[MAXIPBIT] = { 0 };
                                    unsigned char fp_maskbits[MAXIPBIT]= { 0 };

                                    int fp_mask;


                                    Remove_Spaces(value);

                                    fp_ptr = strtok_r(value, ",", &tok);

                                    while ( fp_ptr != NULL )
                                        {

                                            fp_ipblock = strtok_r(fp_ptr, "/", &fp_range);

                                            if ( fp_ipblock == NULL )
                                                {
                                                    Meer_Log(ERROR, "Fingerprint ip block %s is invalid.  Abort", fp_ptr);
                                                }

                                            if (!IP2Bit(fp_ipblock, fp_ipbits))
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] Invalid address %s in 'fingerprint_networks'. Abort", __FILE__, __LINE__, fp_ptr );
                                                }

                                            Fingerprint_Networks = (_Fingerprint_Networks *) realloc(Fingerprint_Networks, (MeerCounters->fingerprint_network_count+1) * sizeof(_Fingerprint_Networks));

                                            if ( Fingerprint_Networks == NULL )
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] Failed to reallocate memory for _Fingerprint_Networks Abort!", __FILE__, __LINE__);
                                                }

                                            memset(&Fingerprint_Networks[MeerCounters->fingerprint_network_count], 0, sizeof(_Fingerprint_Networks));

                                            fp_mask = atoi(fp_range);


                                            if ( fp_mask == 0 || !Mask2Bit(fp_mask, fp_maskbits))
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] Invalid mask for GeoIP 'skip_networks'. Abort", __FILE__, __LINE__);
                                                }



                                            memcpy(Fingerprint_Networks[MeerCounters->fingerprint_network_count].range.ipbits, fp_ipbits, sizeof(fp_ipbits));
                                            memcpy(Fingerprint_Networks[MeerCounters->fingerprint_network_count].range.maskbits, fp_maskbits, sizeof(fp_maskbits));
                                            MeerCounters->fingerprint_network_count++;


                                            fp_ptr = strtok_r(NULL, ",", &tok);

                                        }

                                }

#if defined(HAVE_LIBMYSQLCLIENT) || defined(HAVE_LIBPQ)

                            else if ( !strcmp(last_pass, "health" ) )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->health = true;
                                        }

                                }

                            else if ( !strcmp(last_pass, "health_signatures" ) && MeerConfig->health == true )
                                {


                                    strlcpy(tmp, value, sizeof(tmp));

                                    ptr2 = strtok_r(tmp, ",", &ptr1);

                                    while (ptr2 != NULL )
                                        {

                                            MeerHealth = (_MeerHealth *) realloc(MeerHealth, (MeerCounters->HealthCount+1) * sizeof(_MeerHealth));

                                            MeerHealth[MeerCounters->HealthCount].health_signature = atol(ptr2);

                                            if ( MeerHealth[MeerCounters->HealthCount].health_signature == 0 )
                                                {
                                                    Meer_Log(ERROR, "Invalid 'health_signature' in configuration. Abort");
                                                }

                                            MeerCounters->HealthCount++;

                                            ptr2 = strtok_r(NULL, ",", &ptr1);


                                        }

                                }

#endif

                            else if ( !strcmp(last_pass, "client_stats" ) )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerConfig->client_stats = true;
                                        }

                                }

                        }

#if defined(HAVE_LIBMYSQLCLIENT) || defined(HAVE_LIBPQ)

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_SQL )
                        {

                            if ( !strcmp(last_pass, "enabled" ))
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->sql_enabled = true;
                                        }

                                }


                            if ( !strcmp(last_pass, "reference_system" ))
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->sql_reference_system = true;
                                        }

                                }

                            else if ( !strcmp(last_pass, "sid_file" ) && MeerOutput->sql_reference_system == true )
                                {

                                    strlcpy(MeerOutput->sql_sid_map_file, value, sizeof(MeerOutput->sql_sid_map_file));
                                }

                            else if ( !strcmp(last_pass, "reference" ) && MeerOutput->sql_reference_system == true )
                                {
                                    strlcpy(MeerOutput->sql_reference_file, value, sizeof(MeerOutput->sql_reference_file));
                                }

                            else if ( !strcmp(last_pass, "debug" ) && MeerOutput->sql_enabled == true )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled") )
                                        {
                                            MeerOutput->sql_debug = true;
                                        }
                                }

                            else if ( !strcmp(last_pass, "server" ) && MeerOutput->sql_enabled == true )
                                {
                                    strlcpy(MeerOutput->sql_server, value, sizeof(MeerOutput->sql_server));
                                }

                            else if ( !strcmp(last_pass, "port" ) && MeerOutput->sql_enabled == true )
                                {
                                    MeerOutput->sql_port = atoi(value);
                                }

                            else if ( !strcmp(last_pass, "username" ) && MeerOutput->sql_enabled == true )
                                {
                                    strlcpy(MeerOutput->sql_username, value, sizeof(MeerOutput->sql_username));
                                }

                            else if ( !strcmp(last_pass, "password" ) && MeerOutput->sql_enabled == true )
                                {
                                    strlcpy(MeerOutput->sql_password, value, sizeof(MeerOutput->sql_password));
                                }

                            else if ( !strcmp(last_pass, "database" ) && MeerOutput->sql_enabled == true )
                                {
                                    strlcpy(MeerOutput->sql_database, value, sizeof(MeerOutput->sql_database));
                                }

                            else if ( !strcmp(last_pass, "extra_data" ) && MeerOutput->sql_enabled == true )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->sql_extra_data = true;
                                        }
                                }

                            else if ( !strcmp(last_pass, "fingerprint" ) && MeerOutput->sql_enabled == true )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->sql_fingerprint = true;
                                        }
                                }


                            else if ( !strcmp(last_pass, "reconnect" ) && MeerOutput->sql_enabled == true )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->sql_reconnect = true;
                                        }
                                }

                            else if ( !strcmp(last_pass, "reconnect_time" ) && MeerOutput->sql_enabled == true )
                                {
                                    MeerOutput->sql_reconnect_time = atoi(value);
                                }

                            else if ( !strcmp(last_pass, "driver" ) && MeerOutput->sql_enabled == true )
                                {

#ifdef HAVE_LIBMYSQLCLIENT
                                    if ( !strcmp(value, "mysql" ) )
                                        {
                                            MeerOutput->sql_driver = DB_MYSQL;
                                        }
#endif

#ifndef HAVE_LIBMYSQLCLIENT

                                    if ( !strcasecmp(value, "mysql" ) )
                                        {
                                            Meer_Log(ERROR, "[%s, line %d] Meer isn't compiled into MySQL support.  Abort!", __FILE__, __LINE__);
                                        }

#endif


#ifdef HAVE_LIBPQ

                                    if ( !strcasecmp(value, "postgresql" ) )
                                        {
                                            MeerOutput->sql_driver = DB_POSTGRESQL;
                                        }

#endif

#ifndef HAVE_LIBPQ

                                    if ( !strcasecmp(value, "postgresql" ) )
                                        {
                                            Meer_Log(ERROR, "[%s, line %d] Meer isn't compiled into PostgreSQL support.  Abort!", __FILE__, __LINE__);
                                        }

#endif


                                    if ( MeerOutput->sql_driver == 0 )
                                        {
                                            Meer_Log(ERROR, "SQL driver '%s' is invalid. Abort!", value);
                                        }

                                }

                        }

#endif

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_EXTERNAL )
                        {

                            if ( !strcmp(last_pass, "enabled" ) )
                                {
                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->external_enabled = true;
                                        }
                                }

                            if ( MeerOutput->external_enabled == true && !strcmp(last_pass, "debug" ) )
                                {
                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->external_debug = true;
                                        }
                                }

                            if ( MeerOutput->external_enabled == true && !strcmp(last_pass, "meer_metadata" ) )
                                {
                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->external_meer_metadata_flag = true;
                                        }
                                }


                            if ( MeerOutput->external_enabled == true && !strcmp(last_pass, "program" ) )
                                {

                                    strlcpy(MeerOutput->external_program, value, sizeof(MeerOutput->external_program));
                                }

                            if ( MeerOutput->external_enabled == true && !strcmp(last_pass, "cisco_policies" ) )
                                {

                                    strlcpy(tmp, value, sizeof(tmp));

                                    Remove_Spaces(tmp);

                                    ptr2 = strtok_r(tmp, ",", &ptr1);

                                    while (ptr2 != NULL )
                                        {

                                            if ( !strcmp(ptr2, "policy-security-ips" ) )
                                                {
                                                    MeerOutput->external_metadata_security_ips = true;
                                                    MeerOutput->external_metadata_cisco = true;
                                                }

                                            else if ( !strcmp(ptr2, "policy-max-detect-ips" ) )
                                                {
                                                    MeerOutput->external_metadata_max_detect_ips = true;
                                                    MeerOutput->external_metadata_cisco = true;
                                                }

                                            else if ( !strcmp(ptr2, "policy-connectivity-ips" ) )
                                                {
                                                    MeerOutput->external_metadata_connectivity_ips = true;
                                                    MeerOutput->external_metadata_cisco = true;
                                                }

                                            else if ( !strcmp(ptr2, "policy-balanced-ips" ) )
                                                {
                                                    MeerOutput->external_metadata_balanced_ips = true;
                                                    MeerOutput->external_metadata_cisco = true;
                                                }

                                            ptr2 = strtok_r(NULL, ",", &ptr1);

                                        }

                                }

                            if ( MeerOutput->external_enabled == true && !strcmp(last_pass, "et_signature_severity" ) )
                                {

                                    strlcpy(tmp, value, sizeof(tmp));

                                    Remove_Spaces(tmp);

                                    ptr2 = strtok_r(tmp, ",", &ptr1);

                                    while (ptr2 != NULL )
                                        {

                                            if ( !strcmp(ptr2, "critical" ) )
                                                {
                                                    MeerOutput->external_metadata_et_critical = true;
                                                    MeerOutput->external_metadata_et = true;
                                                }

                                            else if ( !strcmp(ptr2, "major" ) )
                                                {
                                                    MeerOutput->external_metadata_et_major = true;
                                                    MeerOutput->external_metadata_et = true;
                                                }

                                            else if ( !strcmp(ptr2, "minor" ) )
                                                {
                                                    MeerOutput->external_metadata_et_minor = true;
                                                    MeerOutput->external_metadata_et = true;
                                                }

                                            else if ( !strcmp(ptr2, "informational" ) )
                                                {
                                                    MeerOutput->external_metadata_et_informational = true;
                                                    MeerOutput->external_metadata_et = true;
                                                }

                                            ptr2 = strtok_r(NULL, ",", &ptr1);

                                        }
                                }

                            if ( !strcmp(last_pass, "routing" ) && MeerOutput->external_enabled == true )
                                {
                                    routing = true;
                                }

                            if ( routing == true && MeerOutput->external_enabled == true )
                                {

                                    if ( !strcmp(value, "alert" ) )
                                        {
                                            MeerOutput->external_alert = true;
                                        }

                                    else if ( !strcmp(value, "files" ) )
                                        {
                                            MeerOutput->external_files = true;
                                        }

                                    else if ( !strcmp(value, "flow" ) )
                                        {
                                            MeerOutput->external_flow = true;
                                        }

                                    else if ( !strcmp(value, "dns" ) )
                                        {
                                            MeerOutput->external_dns = true;
                                        }

                                    else if ( !strcmp(value, "http" ) )
                                        {
                                            MeerOutput->external_http = true;
                                        }

                                    else if ( !strcmp(value, "tls" ) )
                                        {
                                            MeerOutput->external_tls = true;
                                        }

                                    else if ( !strcmp(value, "ssh" ) )
                                        {
                                            MeerOutput->external_ssh = true;
                                        }

                                    else if ( !strcmp(value, "smtp" ) )
                                        {
                                            MeerOutput->external_smtp = true;
                                        }

                                    else if ( !strcmp(value, "email" ) )
                                        {
                                            MeerOutput->external_email = true;
                                        }

                                    else if ( !strcmp(value, "fileinfo" ) )
                                        {
                                            MeerOutput->external_fileinfo = true;
                                        }

                                    else if ( !strcmp(value, "dhcp" ) )
                                        {
                                            MeerOutput->external_dhcp = true;
                                        }

                                    else if ( !strcmp(value, "stats" ) )
                                        {
                                            MeerOutput->external_stats = true;
                                        }

                                    else if ( !strcmp(value, "rdp" ) )
                                        {
                                            MeerOutput->external_rdp  = true;
                                        }

                                    else if ( !strcmp(value, "sip" ) )
                                        {
                                            MeerOutput->external_sip = true;
                                        }

                                    else if ( !strcmp(value, "ftp" ) )
                                        {
                                            MeerOutput->external_ftp = true;
                                        }

                                    else if ( !strcmp(value, "ikev2" ) )
                                        {
                                            MeerOutput->external_ikev2 = true;
                                        }

                                    else if ( !strcmp(value, "nfs" ) )
                                        {
                                            MeerOutput->external_nfs = true;
                                        }

                                    else if ( !strcmp(value, "tftp" ) )
                                        {
                                            MeerOutput->external_tftp = true;
                                        }

                                    else if ( !strcmp(value, "smb" ) )
                                        {
                                            MeerOutput->external_smb = true;
                                        }

                                    else if ( !strcmp(value, "dcerpc" ) )
                                        {
                                            MeerOutput->external_dcerpc = true;
                                        }

                                    else if ( !strcmp(value, "mqtt" ) )
                                        {
                                            MeerOutput->external_mqtt = true;
                                        }

                                    else if ( !strcmp(value, "netflow" ) )
                                        {
                                            MeerOutput->external_netflow = true;
                                        }

                                    else if ( !strcmp(value, "metadata" ) )
                                        {
                                            MeerOutput->external_metadata = true;
                                        }

                                    else if ( !strcmp(value, "dnp3" ) )
                                        {
                                            MeerOutput->external_dnp3 = true;
                                        }

                                    else if ( !strcmp(value, "anomaly" ) )
                                        {
                                            MeerOutput->external_anomaly = true;
                                        }

                                    else if ( !strcmp(value, "fingerprint" ) )
                                        {
                                            MeerOutput->external_fingerprint = true;
                                        }
                                }
                        }


#ifndef HAVE_LIBHIREDIS

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_REDIS )
                        {

                            if (!strcmp(last_pass, "enabled"))
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            Meer_Log(ERROR, "[%s, line %d] Meer was not compiled with hiredis (Redis) support!", __FILE__, __LINE__);
                                        }

                                }
                        }

#endif

#ifdef HAVE_LIBHIREDIS

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_REDIS )
                        {

                            if (!strcmp(last_pass, "enabled"))
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->redis_enabled = true;
                                        }
                                }

                            if (!strcmp(last_pass, "debug") && MeerOutput->redis_enabled == true )
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->redis_debug = true;
                                        }
                                }

                            if ( !strcmp(last_pass, "server") && MeerOutput->redis_enabled == true )
                                {
                                    strlcpy(MeerOutput->redis_server, value, sizeof(MeerOutput->redis_server));
                                }

                            if ( !strcmp(last_pass, "password") && MeerOutput->redis_enabled == true )
                                {
                                    strlcpy(MeerOutput->redis_password, value, sizeof(MeerOutput->redis_password));
                                }

                            if ( !strcmp(last_pass, "key") && MeerOutput->redis_enabled == true )
                                {
                                    strlcpy(MeerOutput->redis_key, value, sizeof(MeerOutput->redis_key));
                                }

                            if ( !strcmp(last_pass, "mode") && MeerOutput->redis_enabled == true )
                                {
                                    if ( strcmp(value, "list") && strcmp(value, "lpush") &&
                                            strcmp(value, "rpush" ) && strcmp(value, "channel") &&
                                            strcmp(value, "publish" ) && strcmp(value, "set"  ) )
                                        {
                                            Meer_Log(ERROR, "Invalid 'redis' -> 'mode'.  Must be list, lpush, rpush, channel, public or set . Abort");
                                        }

                                    if ( !strcmp(value, "list") || !strcmp(value, "lpush" ) )
                                        {
                                            strlcpy( MeerOutput->redis_command, "lpush", sizeof(MeerOutput->redis_command) );
                                        }

                                    if ( !strcmp(value, "rpush"))
                                        {
                                            strlcpy( MeerOutput->redis_command, "rpush", sizeof(MeerOutput->redis_command) );
                                        }

                                    if ( !strcmp(value, "channel") || !strcmp(value, "publish" ) )
                                        {
                                            strlcpy( MeerOutput->redis_command, "publish", sizeof(MeerOutput->redis_command) );
                                        }

                                    if ( !strcmp(value, "set") )
                                        {
                                            strlcpy( MeerOutput->redis_command, "set", sizeof(MeerOutput->redis_command) );
                                        }

                                }

                            if ( !strcmp(last_pass, "append_id" ) && MeerOutput->redis_enabled == true )
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->redis_append_id = true;
                                        }
                                }

                            if ( !strcmp(last_pass, "port" ) && MeerOutput->redis_enabled == true )
                                {

                                    MeerOutput->redis_port = atoi(value);

                                    if ( MeerOutput->redis_port == 0 )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  redis -> port is invalid");
                                        }
                                }

                            if ( !strcmp(last_pass, "batch" ) && MeerOutput->redis_enabled == true )
                                {

                                    MeerOutput->redis_batch = atoi(value);

                                    if ( MeerOutput->redis_batch == 0 )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  redis -> batch is invalid");
                                        }
                                }

                            if ( !strcmp(last_pass, "routing" ) && MeerOutput->redis_enabled == true )
                                {
                                    routing = true;
                                }

                            if ( routing == true && MeerOutput->redis_enabled == true )
                                {

                                    if ( !strcmp(value, "alert" ) )
                                        {
                                            MeerOutput->redis_alert = true;
                                        }

                                    else if ( !strcmp(value, "files" ) )
                                        {
                                            MeerOutput->redis_files = true;
                                        }

                                    else if ( !strcmp(value, "flow" ) )
                                        {
                                            MeerOutput->redis_flow = true;
                                        }

                                    else if ( !strcmp(value, "dns" ) )
                                        {
                                            MeerOutput->redis_dns = true;
                                        }

                                    else if ( !strcmp(value, "http" ) )
                                        {
                                            MeerOutput->redis_http = true;
                                        }

                                    else if ( !strcmp(value, "tls" ) )
                                        {
                                            MeerOutput->redis_tls = true;
                                        }

                                    else if ( !strcmp(value, "ssh" ) )
                                        {
                                            MeerOutput->redis_ssh = true;
                                        }

                                    else if ( !strcmp(value, "smtp" ) )
                                        {
                                            MeerOutput->redis_smtp = true;
                                        }

                                    else if ( !strcmp(value, "email" ) )
                                        {
                                            MeerOutput->redis_email = true;
                                        }

                                    else if ( !strcmp(value, "fileinfo" ) )
                                        {
                                            MeerOutput->redis_fileinfo = true;
                                        }

                                    else if ( !strcmp(value, "dhcp" ) )
                                        {
                                            MeerOutput->redis_dhcp = true;
                                        }

                                    else if ( !strcmp(value, "stats" ) )
                                        {
                                            MeerOutput->redis_stats = true;
                                        }

                                    else if ( !strcmp(value, "rdp" ) )
                                        {
                                            MeerOutput->redis_rdp  = true;
                                        }

                                    else if ( !strcmp(value, "sip" ) )
                                        {
                                            MeerOutput->redis_sip = true;
                                        }

                                    else if ( !strcmp(value, "ftp" ) )
                                        {
                                            MeerOutput->redis_ftp = true;
                                        }

                                    else if ( !strcmp(value, "ikev2" ) )
                                        {
                                            MeerOutput->redis_ikev2 = true;
                                        }

                                    else if ( !strcmp(value, "nfs" ) )
                                        {
                                            MeerOutput->redis_nfs = true;
                                        }

                                    else if ( !strcmp(value, "tftp" ) )
                                        {
                                            MeerOutput->redis_tftp = true;
                                        }

                                    else if ( !strcmp(value, "smb" ) )
                                        {
                                            MeerOutput->redis_smb = true;
                                        }

                                    else if ( !strcmp(value, "dcerpc" ) )
                                        {
                                            MeerOutput->redis_dcerpc = true;
                                        }

                                    else if ( !strcmp(value, "mqtt" ) )
                                        {
                                            MeerOutput->redis_mqtt = true;
                                        }

                                    else if ( !strcmp(value, "netflow" ) )
                                        {
                                            MeerOutput->redis_netflow = true;
                                        }

                                    else if ( !strcmp(value, "metadata" ) )
                                        {
                                            MeerOutput->redis_metadata = true;
                                        }

                                    else if ( !strcmp(value, "dnp3" ) )
                                        {
                                            MeerOutput->redis_dnp3 = true;
                                        }

                                    else if ( !strcmp(value, "anomaly" ) )
                                        {
                                            MeerOutput->redis_anomaly = true;
                                        }

                                    else if ( !strcmp(value, "fingerprint" ) )
                                        {
                                            MeerOutput->redis_fingerprint = true;
                                        }

                                    else if ( !strcmp(value, "client_stats" ) )
                                        {
                                            MeerOutput->redis_client_stats = true;
                                        }

                                }
                        }

#endif


#ifndef WITH_ELASTICSEARCH

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_ELASTICSEARCH )
                        {

                            if (!strcmp(last_pass, "enabled"))
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            Meer_Log(ERROR, "[%s, line %d] Meer was not compiled with Elasticsearch support!", __FILE__, __LINE__);
                                        }

                                }
                        }

#endif

#ifdef WITH_ELASTICSEARCH

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_ELASTICSEARCH )
                        {
                            if (!strcmp(last_pass, "enabled"))
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->elasticsearch_enabled = true;
                                        }
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "debug") )
                                {

                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->elasticsearch_debug = true;
                                        }
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "insecure") )
                                {

                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->elasticsearch_insecure = true;
                                        }
                                }


                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "url") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'elasticsearch' url is invalid");
                                        }

                                    strlcpy(MeerOutput->elasticsearch_url, value, sizeof(MeerOutput->elasticsearch_url));
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "index") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'elasticsearch' index is invalid");
                                        }

                                    strlcpy(MeerOutput->elasticsearch_index, value, sizeof(MeerOutput->elasticsearch_index));
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "username") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration. 'elasticsearch' username is invalid");
                                        }

                                    strlcpy(MeerOutput->elasticsearch_username, value, sizeof(MeerOutput->elasticsearch_username));
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "password") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration. 'elasticsearch' password is invalid");
                                        }

                                    strlcpy(MeerOutput->elasticsearch_password, value, sizeof(MeerOutput->elasticsearch_password));
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "batch") )
                                {

                                    MeerOutput->elasticsearch_batch = atoi(value);

                                    if ( MeerOutput->elasticsearch_batch == 0 )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration. 'elasticsearch' batch is invalid");
                                        }
                                }

                            if ( MeerOutput->elasticsearch_enabled == true && !strcmp(last_pass, "threads") )
                                {

                                    MeerOutput->elasticsearch_threads = atoi(value);

                                    if ( MeerOutput->elasticsearch_threads == 0 )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration. 'elasticsearch' threads is invalid");
                                        }
                                }

                            if ( !strcmp(last_pass, "routing" ) && MeerOutput->elasticsearch_enabled == true )
                                {
                                    routing = true;
                                }

                            if ( routing == true && MeerOutput->elasticsearch_enabled == true )
                                {

                                    if ( !strcmp(value, "alert" ) )
                                        {
                                            MeerOutput->elasticsearch_alert = true;
                                        }

                                    else if ( !strcmp(value, "files" ) )
                                        {
                                            MeerOutput->elasticsearch_files = true;
                                        }

                                    else if ( !strcmp(value, "flow" ) )
                                        {
                                            MeerOutput->elasticsearch_flow = true;
                                        }

                                    else if ( !strcmp(value, "dns" ) )
                                        {
                                            MeerOutput->elasticsearch_dns = true;
                                        }

                                    else if ( !strcmp(value, "http" ) )
                                        {
                                            MeerOutput->elasticsearch_http = true;
                                        }

                                    else if ( !strcmp(value, "tls" ) )
                                        {
                                            MeerOutput->elasticsearch_tls = true;
                                        }

                                    else if ( !strcmp(value, "ssh" ) )
                                        {
                                            MeerOutput->elasticsearch_ssh = true;
                                        }

                                    else if ( !strcmp(value, "smtp" ) )
                                        {
                                            MeerOutput->elasticsearch_smtp = true;
                                        }

                                    else if ( !strcmp(value, "email" ) )
                                        {
                                            MeerOutput->elasticsearch_email = true;
                                        }

                                    else if ( !strcmp(value, "fileinfo" ) )
                                        {
                                            MeerOutput->elasticsearch_fileinfo = true;
                                        }

                                    else if ( !strcmp(value, "dhcp" ) )
                                        {
                                            MeerOutput->elasticsearch_dhcp = true;
                                        }

                                    else if ( !strcmp(value, "stats" ) )
                                        {
                                            MeerOutput->elasticsearch_stats = true;
                                        }

                                    else if ( !strcmp(value, "rdp" ) )
                                        {
                                            MeerOutput->elasticsearch_rdp  = true;
                                        }

                                    else if ( !strcmp(value, "sip" ) )
                                        {
                                            MeerOutput->elasticsearch_sip = true;
                                        }

                                    else if ( !strcmp(value, "ftp" ) )
                                        {
                                            MeerOutput->elasticsearch_ftp = true;
                                        }

                                    else if ( !strcmp(value, "ikev2" ) )
                                        {
                                            MeerOutput->elasticsearch_ikev2 = true;
                                        }

                                    else if ( !strcmp(value, "nfs" ) )
                                        {
                                            MeerOutput->elasticsearch_nfs = true;
                                        }

                                    else if ( !strcmp(value, "tftp" ) )
                                        {
                                            MeerOutput->elasticsearch_tftp = true;
                                        }

                                    else if ( !strcmp(value, "smb" ) )
                                        {
                                            MeerOutput->elasticsearch_smb = true;
                                        }

                                    else if ( !strcmp(value, "dcerpc" ) )
                                        {
                                            MeerOutput->elasticsearch_dcerpc = true;
                                        }

                                    else if ( !strcmp(value, "mqtt" ) )
                                        {
                                            MeerOutput->elasticsearch_mqtt = true;
                                        }

                                    else if ( !strcmp(value, "netflow" ) )
                                        {
                                            MeerOutput->elasticsearch_netflow = true;
                                        }

                                    else if ( !strcmp(value, "metadata" ) )
                                        {
                                            MeerOutput->elasticsearch_metadata = true;
                                        }

                                    else if ( !strcmp(value, "dnp3" ) )
                                        {
                                            MeerOutput->elasticsearch_dnp3 = true;
                                        }

                                    else if ( !strcmp(value, "anomaly" ) )
                                        {
                                            MeerOutput->elasticsearch_anomaly = true;
                                        }

                                    else if ( !strcmp(value, "fingerprint" ) )
                                        {
                                            MeerOutput->elasticsearch_fingerprint = true;
                                        }


                                }

                        }

#endif

#ifdef WITH_BLUEDOT

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_BLUEDOT )
                        {

                            if (!strcmp(last_pass, "enabled"))
                                {
                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->bluedot_flag = true;
                                        }
                                }

                            if ( MeerOutput->bluedot_flag == true && !strcmp(last_pass, "debug") )
                                {

                                    if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->bluedot_debug = true;
                                        }
                                }


                            if ( MeerOutput->bluedot_flag == true && !strcmp(last_pass, "host") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'bluedot' host is invalid");
                                        }

                                    strlcpy(MeerOutput->bluedot_host, value, sizeof(MeerOutput->bluedot_host));
                                }

                            if ( MeerOutput->bluedot_flag == true && !strcmp(last_pass, "uri") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'bluedot' uri is invalid");
                                        }

                                    strlcpy(MeerOutput->bluedot_uri, value, sizeof(MeerOutput->bluedot_uri));
                                }

                            if ( MeerOutput->bluedot_flag == true && !strcmp(last_pass, "source") )
                                {

                                    if ( value[0] == '\0' )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'bluedot' source is invalid");
                                        }

                                    strlcpy(MeerOutput->bluedot_source, value, sizeof(MeerOutput->bluedot_source));
                                }


                            if ( MeerOutput->bluedot_flag == true && !strcmp(last_pass, "skip_networks" ) )
                                {

                                    Remove_Spaces(value);

                                    int  bluedot_mask = 0;

                                    char *tok = NULL;
                                    char *bluedot_iprange = NULL;
                                    char *bluedot_tok = NULL;
                                    char *bluedot_tmpmask = NULL;

                                    unsigned char bluedot_ipbits[MAXIPBIT] = { 0 };
                                    unsigned char bluedot_maskbits[MAXIPBIT]= { 0 };

                                    char *bluedot_ptr = strtok_r(value, ",", &tok);

                                    while ( bluedot_ptr != NULL )
                                        {

                                            bluedot_iprange = strtok_r(bluedot_ptr, "/", &bluedot_tok);

                                            if ( bluedot_iprange == NULL )
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] 'bluedot' - 'skip_networks' is invalid. Abort.", __FILE__, __LINE__);
                                                }

                                            if (!IP2Bit(bluedot_iprange, bluedot_ipbits))
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] 'bluedot' - 'skip_address' is invalid. Abort", __FILE__, __LINE__);
                                                }

                                            bluedot_tmpmask = strtok_r(NULL, "/", &bluedot_tok);

                                            if ( bluedot_tmpmask == NULL )
                                                {
                                                    bluedot_mask = 32;
                                                }

                                            Bluedot_Skip = (_Bluedot_Skip *) realloc(Bluedot_Skip, (MeerCounters->bluedot_skip_count+1) * sizeof(_Bluedot_Skip));

                                            if ( Bluedot_Skip == NULL )
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] 'bluedot' - Failed to reallocate memory for Bluedot_Skip Abort!", __FILE__, __LINE__);
                                                }

                                            memset(&Bluedot_Skip[MeerCounters->bluedot_skip_count], 0, sizeof(_Bluedot_Skip));

                                            bluedot_mask = atoi(bluedot_tmpmask);

                                            if ( bluedot_mask == 0 || !Mask2Bit(bluedot_mask, bluedot_maskbits))
                                                {
                                                    Meer_Log(ERROR, "[%s, line %d] Invalid mask for Bluedot 'skip_networks'. Abort", __FILE__, __LINE__);
                                                }


                                            memcpy(Bluedot_Skip[MeerCounters->bluedot_skip_count].range.ipbits, bluedot_ipbits, sizeof(bluedot_ipbits));
                                            memcpy(Bluedot_Skip[MeerCounters->bluedot_skip_count].range.maskbits, bluedot_maskbits, sizeof(bluedot_maskbits));

                                            MeerCounters->bluedot_skip_count++;

                                            bluedot_ptr = strtok_r(NULL, ",", &tok);

                                        }

                                }

                        }

#endif

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_FILE )
                        {

                            if ( !strcmp(last_pass, "enabled" ) )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->file_enabled = true;
                                        }
                                }

                            if ( !strcmp(last_pass, "file_location") && MeerOutput->file_enabled == true )
                                {
                                    strlcpy(MeerOutput->file_location, value, sizeof(MeerOutput->file_location));
                                }

                            if ( !strcmp(last_pass, "routing" ) && MeerOutput->file_enabled == true )
                                {
                                    routing = true;
                                }

                            if ( routing == true && MeerOutput->file_enabled == true )
                                {

                                    if ( !strcmp(value, "alert" ) )
                                        {
                                            MeerOutput->file_alert = true;
                                        }

                                    else if ( !strcmp(value, "files" ) )
                                        {
                                            MeerOutput->file_files = true;
                                        }

                                    else if ( !strcmp(value, "flow" ) )
                                        {
                                            MeerOutput->file_flow = true;
                                        }

                                    else if ( !strcmp(value, "dns" ) )
                                        {
                                            MeerOutput->file_dns = true;
                                        }

                                    else if ( !strcmp(value, "http" ) )
                                        {
                                            MeerOutput->file_http = true;
                                        }

                                    else if ( !strcmp(value, "tls" ) )
                                        {
                                            MeerOutput->file_tls = true;
                                        }

                                    else if ( !strcmp(value, "ssh" ) )
                                        {
                                            MeerOutput->file_ssh = true;
                                        }

                                    else if ( !strcmp(value, "smtp" ) )
                                        {
                                            MeerOutput->file_smtp = true;
                                        }

                                    else if ( !strcmp(value, "email" ) )
                                        {
                                            MeerOutput->file_email = true;
                                        }

                                    else if ( !strcmp(value, "fileinfo" ) )
                                        {
                                            MeerOutput->file_fileinfo = true;
                                        }

                                    else if ( !strcmp(value, "dhcp" ) )
                                        {
                                            MeerOutput->file_dhcp = true;
                                        }

                                    else if ( !strcmp(value, "stats" ) )
                                        {
                                            MeerOutput->file_stats = true;
                                        }

                                    else if ( !strcmp(value, "rdp" ) )
                                        {
                                            MeerOutput->file_rdp  = true;
                                        }

                                    else if ( !strcmp(value, "sip" ) )
                                        {
                                            MeerOutput->file_sip = true;
                                        }

                                    else if ( !strcmp(value, "ftp" ) )
                                        {
                                            MeerOutput->file_ftp = true;
                                        }

                                    else if ( !strcmp(value, "ikev2" ) )
                                        {
                                            MeerOutput->file_ikev2 = true;
                                        }

                                    else if ( !strcmp(value, "nfs" ) )
                                        {
                                            MeerOutput->file_nfs = true;
                                        }

                                    else if ( !strcmp(value, "tftp" ) )
                                        {
                                            MeerOutput->file_tftp = true;
                                        }

                                    else if ( !strcmp(value, "smb" ) )
                                        {
                                            MeerOutput->file_smb = true;
                                        }

                                    else if ( !strcmp(value, "dcerpc" ) )
                                        {
                                            MeerOutput->file_dcerpc = true;
                                        }

                                    else if ( !strcmp(value, "mqtt" ) )
                                        {
                                            MeerOutput->file_mqtt = true;
                                        }

                                    else if ( !strcmp(value, "netflow" ) )
                                        {
                                            MeerOutput->file_netflow = true;
                                        }

                                    else if ( !strcmp(value, "metadata" ) )
                                        {
                                            MeerOutput->file_metadata = true;
                                        }

                                    else if ( !strcmp(value, "dnp3" ) )
                                        {
                                            MeerOutput->file_dnp3 = true;
                                        }

                                    else if ( !strcmp(value, "anomaly" ) )
                                        {
                                            MeerOutput->file_anomaly = true;
                                        }

                                    else if ( !strcmp(value, "fingerprint" ) )
                                        {
                                            MeerOutput->file_fingerprint = true;
                                        }

                                }
                        }

                    if ( type == YAML_TYPE_OUTPUT && sub_type == YAML_MEER_PIPE )
                        {

                            if ( !strcmp(last_pass, "enabled" ) )
                                {

                                    if ( !strcasecmp(value, "yes") || !strcasecmp(value, "true" ) || !strcasecmp(value, "enabled"))
                                        {
                                            MeerOutput->pipe_enabled = true;
                                        }

                                }

                            if ( !strcmp(last_pass, "pipe_location") && MeerOutput->pipe_enabled == true )
                                {
                                    strlcpy(MeerOutput->pipe_location, value, sizeof(MeerOutput->pipe_location));
                                }

                            if ( !strcmp(last_pass, "pipe_size" ) && MeerOutput->pipe_enabled == true )
                                {

                                    MeerOutput->pipe_size = atoi(value);

                                    if ( MeerOutput->pipe_size == 0 )
                                        {
                                            Meer_Log(ERROR, "Invalid configuration.  'pipe_size' is invalid");
                                        }


                                    if ( MeerOutput->pipe_size != 65536 &&
                                            MeerOutput->pipe_size != 131072 &&
                                            MeerOutput->pipe_size != 262144 &&
                                            MeerOutput->pipe_size != 524288 &&
                                            MeerOutput->pipe_size != 1048576 )
                                        {

                                            Meer_Log(ERROR, "Invalid configuration. 'pipe_size' must be 65536, 131072, 262144, 524288 or 1048576. Abort!");
                                        }


                                }

                            if ( !strcmp(last_pass, "routing" ) && MeerOutput->pipe_enabled == true )
                                {
                                    routing = true;
                                }

                            if ( routing == true && MeerOutput->pipe_enabled == true )
                                {

                                    if ( !strcmp(value, "alert" ) )
                                        {
                                            MeerOutput->pipe_alert = true;
                                        }

                                    else if ( !strcmp(value, "files" ) )
                                        {
                                            MeerOutput->pipe_files = true;
                                        }

                                    else if ( !strcmp(value, "flow" ) )
                                        {
                                            MeerOutput->pipe_flow = true;
                                        }

                                    else if ( !strcmp(value, "dns" ) )
                                        {
                                            MeerOutput->pipe_dns = true;
                                        }

                                    else if ( !strcmp(value, "http" ) )
                                        {
                                            MeerOutput->pipe_http = true;
                                        }

                                    else if ( !strcmp(value, "tls" ) )
                                        {
                                            MeerOutput->pipe_tls = true;
                                        }

                                    else if ( !strcmp(value, "ssh" ) )
                                        {
                                            MeerOutput->pipe_ssh = true;
                                        }

                                    else if ( !strcmp(value, "smtp" ) )
                                        {
                                            MeerOutput->pipe_smtp = true;
                                        }

                                    else if ( !strcmp(value, "email" ) )
                                        {
                                            MeerOutput->pipe_email = true;
                                        }

                                    else if ( !strcmp(value, "fileinfo" ) )
                                        {
                                            MeerOutput->pipe_fileinfo = true;
                                        }

                                    else if ( !strcmp(value, "dhcp" ) )
                                        {
                                            MeerOutput->pipe_dhcp = true;
                                        }

                                    else if ( !strcmp(value, "stats" ) )
                                        {
                                            MeerOutput->pipe_stats = true;
                                        }

                                    else if ( !strcmp(value, "rdp" ) )
                                        {
                                            MeerOutput->pipe_rdp  = true;
                                        }

                                    else if ( !strcmp(value, "sip" ) )
                                        {
                                            MeerOutput->pipe_sip = true;
                                        }

                                    else if ( !strcmp(value, "ftp" ) )
                                        {
                                            MeerOutput->pipe_ftp = true;
                                        }

                                    else if ( !strcmp(value, "ikev2" ) )
                                        {
                                            MeerOutput->pipe_ikev2 = true;
                                        }

                                    else if ( !strcmp(value, "nfs" ) )
                                        {
                                            MeerOutput->pipe_nfs = true;
                                        }

                                    else if ( !strcmp(value, "tftp" ) )
                                        {
                                            MeerOutput->pipe_tftp = true;
                                        }

                                    else if ( !strcmp(value, "smb" ) )
                                        {
                                            MeerOutput->pipe_smb = true;
                                        }

                                    else if ( !strcmp(value, "dcerpc" ) )
                                        {
                                            MeerOutput->pipe_dcerpc = true;
                                        }

                                    else if ( !strcmp(value, "mqtt" ) )
                                        {
                                            MeerOutput->pipe_mqtt = true;
                                        }

                                    else if ( !strcmp(value, "netflow" ) )
                                        {
                                            MeerOutput->pipe_netflow = true;
                                        }

                                    else if ( !strcmp(value, "metadata" ) )
                                        {
                                            MeerOutput->pipe_metadata = true;
                                        }

                                    else if ( !strcmp(value, "dnp3" ) )
                                        {
                                            MeerOutput->pipe_dnp3 = true;
                                        }

                                    else if ( !strcmp(value, "anomaly" ) )
                                        {
                                            MeerOutput->pipe_anomaly = true;
                                        }

                                    else if ( !strcmp(value, "fingerprint" ) )
                                        {
                                            MeerOutput->pipe_fingerprint = true;
                                        }
                                }

                        }

                    strlcpy(last_pass, value, sizeof(last_pass));

                } /* end of else */



        }

    /* Sanity check on core configurations */

    if ( MeerConfig->interface[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'interface' specified!");
        }

    if ( MeerConfig->hostname[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'hostname' specified!");
        }

    if ( MeerConfig->runas[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'runas' specified!");
        }

    if ( MeerConfig->classification_file[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'classification' file specified!");
        }

    if ( MeerConfig->waldo_file[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'waldo-file' specified!");
        }

    if ( MeerConfig->follow_file[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'follow-exe' file specified!");
        }

    if ( MeerConfig->lock_file[0] == '\0' )
        {
            Meer_Log(ERROR, "Configuration incomplete.  No 'lock-file' file specified!");
        }

#ifdef HAVE_LIBMYSQLCLIENT

    if ( MeerOutput->sql_enabled == true )
        {

            if ( MeerOutput->sql_server[0] == '\0' )
                {
                    Meer_Log(ERROR, "SQL output configuration incomplete.  No 'server' specified!");
                }

            if ( MeerOutput->sql_username[0] == '\0' )
                {
                    Meer_Log(ERROR, "SQL output configuration incomplete.  No 'username' specified!");
                }


            if ( MeerOutput->sql_password[0] == '\0' )
                {
                    Meer_Log(ERROR, "SQL output configuration incomplete.  No 'password' specified!");
                }

            if ( MeerOutput->sql_database[0] == '\0' )
                {
                    Meer_Log(ERROR, "SQL output configuration incomplete.  No 'database' specified!");
                }

            if ( MeerOutput->sql_port == 0 )
                {
                    Meer_Log(ERROR, "SQL output configuration incomplete.  No 'port' specified!");
                }
        }

#endif

    Meer_Log(NORMAL, "Configuration '%s' for host '%s' successfully loaded.", yaml_file, MeerConfig->hostname);

}
