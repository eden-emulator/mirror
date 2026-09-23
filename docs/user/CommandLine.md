# User Handbook - Command Line

There are two main applications, an SDL-based app (`eden-cli`) and a Qt based app (`eden`); both accept the same command line arguments.

- `./eden <path>`: Running with a single argument and nothing else, will make the emulator look for the given file and load it, this behavior is similar to `eden-cli`; allows dragging and dropping games into the application.
- `--debug/-d`: Enter debug mode, allow gdb stub at port `1234`
- `--config/-c`: Specify alternate configuration file.
- `--fullscreen/-f`: Use fullscreen.
- `--help/-h`: Display help.
- `--game/-g <path>`: Alternate way to specify what to load, overrides. However let it be noted that arguments that use `-` will be treated as options/ignored, if your game, for some reason, starts with `-`, in order to safely handle it you may need to specify it as an argument.
- `--multiplayer/-m`: Specify multiplayer options.
- `--program/-p`: Specify the program arguments to pass (optional).
- `--user/-u <number>`: Specify the user index.
- `--version/-v`: Display version and quit.
- `--input-profile/-i <name>`: Specifies input profile name to use (for player #0 only).
- `--null-render/-n`: Forces the usage of the "Null" render backend irrespective of settings.
- `--filter/-x`: Sets the debug log filter irrespective of settings.
- `--singlecore/-s`: Forces single-core regardless of settings.

Only the Qt frontend supports the following arguments:

- `-qlaunch`: Launch QLaunch.
- `-hlaunch`: Launch homebrew launcher `nx-hbloader`.
    - Requires a copy of Atmosphere to be extracted onto `sdmc`.
    - This is a shorthand for `<eden folder>/sdmc/atmosphere/hbl.nsp`.
- `-setup`: Launch setup applet.

`eden` (provided with `--room`), and `eden-room` supports the following options as well:

- `-n/--room-name`: The name of the room.
- `-d/--room-description`: The room description.
- `-s/--bind-address`: The bind address for the room.
- `-p/--port`: The port used for the room.
- `-m/--max-members`: The maximum number of players for this room.
- `-w/--password`: The password for the room.
- `-g/--preferred-game`: The preferred game for this room.
- `-i/--preferred-game-id`: The preferred game-id for this room.
- `-u/--username`: The username used for announce.
- `-t/--token`: The token used for announce.
- `-a/--web-api-url`: yuzu Web API url.
- `-b/--ban-list-file`: The file for storing the room ban list.
- `-l/--log-file`: The file for storing the room log.
- `-h/--help`: Display this help and exit.
- `-v/--version`: Output version information and exit.
