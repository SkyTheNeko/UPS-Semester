/**
 * @file config.h
 * @brief Config management
 * 
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char ip[64];        // Bind IP address
    int  port;          // TCP port
    int  max_clients;   // Maximum number of clients
    int  max_rooms;     // Maximum number of rooms
} ServerConfig;

/**
 * @brief Fills configuration with built-in defaults
 *
 * @param cfg   Pointer to configuration structure to initialize
 */
void config_defaults(ServerConfig* cfg);

/**
 * @brief Loads configuration values from a simple ini-like file
 *
 * @param cfg   Pointer to configuration structure to update
 * @param path  Path to the config file ("server.ini")
 *
 * @return 1 if the file was loaded successfully, 0 otherwise.
 */
int  config_load_file(ServerConfig* cfg, const char* path);

/**
 * @brief Prints the current configuration
 *
 * @param cfg Pointer to configuration structure to print
 */
void config_print(const ServerConfig* cfg);

#endif
