# WiFi and Server Authentication Configuration

This project now uses a flexible WiFi connection system with automatic fallback and reboot functionality for improved reliability.

## Quick Setup

1. Copy the example secrets file:
   ```bash
   cp include/secrets.h.example include/secrets.h
   ```

2. Edit `include/secrets.h` with your network credentials:
   ```cpp
   static const WiFiNetwork WIFI_NETWORKS[] = {
       {"YourPrimaryNetwork", "YourPassword"},
       {"YourBackupNetwork", "BackupPassword"},
       // Add more networks as needed
   };
   ```

3. Update server credentials if needed:
   ```cpp
   #define SERVER_USERNAME "your_username"
   #define SERVER_PASSWORD "your_password"
   ```

## How It Works

### WiFi Connection
- The system tries each network in the `WIFI_NETWORKS` array in order
- If a network connection fails, it moves to the next one
- If all networks fail, the device reboots after 5 seconds

### Server Authentication
- After WiFi connection, the device attempts to authenticate with the game server
- If server authentication fails, the device reboots after 5 seconds
- This ensures the device doesn't run in a degraded state

### Automatic Recovery
- Both WiFi and server failures trigger automatic reboots
- This ensures the system continuously tries to establish proper connectivity
- Useful for temporary network outages or server maintenance

## Security

- `include/secrets.h` is automatically ignored by git (listed in `.gitignore`)
- Never commit your actual credentials to version control
- Use the `.example` file as a template for new deployments

## Network Priority

Networks are tried in the order they appear in the array:
1. First network in the list gets highest priority
2. Backup networks are tried if primary fails
3. Add more networks as needed for different deployment environments

## Troubleshooting

If the device keeps rebooting:
1. Check that at least one WiFi network in your list is available
2. Verify WiFi passwords are correct
3. Ensure the game server is running and accessible
4. Check server username/password credentials

The serial output will show detailed connection attempts and failure reasons.
