# User Handbook - Command Line

There are two main applications, an SDL-based app (`eden-cli`) and a Qt based app (`eden`); both accept command line arguments and share almost the same accepted arguments.

## eden

- `./eden <path>`: Running with a single argument and nothing else, will make the emulator look for the given file and load it, this behaviour is similar to `eden-cli`; allows dragging and dropping games into the application.
- `--hlaunch`: Launch homebrew launcher `nx-hbloader`.
    - Requires a copy of Atmosphere to be extracted onto `sdmc`.
    - This is a shorthand for `<eden folder>/sdmc/atmosphere/hbl.nsp`.
- `--setup`: Launch setup applet.
- `-q/--qlaunch`: Launch QLaunch.
- `-d/--debug`: Enter debug mode, allow gdb stub at port `1234`
- `-c/--config`: Specify alternate configuration file.
- `-f/--fullscreen`: Set fullscreen.
- `-h/--help`: Display help.
- `-g/--game`: Alternate way to specify what to load, overrides. However let it be noted that arguments that use `-` will be treated as options/ignored, if your game, for some reason, starts with `-`, in order to safely handle it you may need to specify it as an argument.
- `-m/--multiplayer`: Specify multiplayer options.
- `-p/--program`: Specify the program arguments to pass (optional).
- `-u/--user`: Select the index of the user to load as.
- `-v/--version`: Display version and quit.
- `-i/--input-profile`: Specifies input profile name to use (for player #0 only).
- `-n/--null-render`: Forces the usage of the "Null" render backend irrespective of settings.
- `-x/--filter`: Sets the debug log filter irrespective of settings.
- `-s/--singlecore`: Forces single-core regardless of settings.
- `-l/--log-file`: The file for storing the room log.
- `-H/--headless`: Force headless mode (no GUI). Currently only used for rooms.

Room settings:
- `-N/--name`: The name of the room.
- `-D/--description`: The room description.
- `-S/--bind-address`: The bind address for the room.
- `-P/--port`: The port used for the room.
- `-M/--max-members`: The maximum number of players for this room.
- `-W/--password`: The password for the room.
- `-G/--preferred-game`: The preferred game for this room.
- `-I/--preferred-game-id`: The preferred game-id for this room.
- `-U/--username`: The username used for announce.
- `-T/--token`: The token used for announce.
- `-A/--web-api-url`: yuzu Web API url.
- `-B/--ban-list-file`: The file for storing the room ban list.
- `-h/--help`: Display this help and exit.
- `-v/--version`: Output version information and exit.,

If the name and description of the room is specified, then it will default to headless mode if not already specified.

Old settings `-hlaunch`, `-qlaunch` and `-setup` are recognized independently. They're kept for backwards compatibility with shortcuts made before the change. Using one of these makes the parser immediately halt and ignore every other option.
