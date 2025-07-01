[system programming lecture]

-project 2 baseline

csapp.{c,h}
        CS:APP3e functions

myshell.c
        Simple shell example

## Build
```bash
make
```


## Run
```bash
./myshell
```

## Manual
- **prompt**: `CSE4100-SP-P2> `
- **run command**: `ls -al`, `./a.out` 
- **pipeline**: `command1 | command2` (ex: `ls -al | grep .c`)
- **run in background**: `command &` (ex: `sleep 10 &`)

## Job control commands 
- `jobs`: List current background and stopped jobs
- `fg %<jid>` or `fg <pid>`: Bring the specified job to the foreground
- `bg %<jid>` or `bg <pid>`: Resume the specified job in the background
- `kill %<jid>` : Sends the SIGTERM signal to the specified job

## Builtin commands
- `cd [directory]` : Change the working directory (supports `cd`, `cd ~`, `cd $VAR`)
- `exit` or `quit` : Exit the shell

