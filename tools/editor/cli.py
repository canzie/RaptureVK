#!/usr/bin/env python3
"""Issue a command to a running Rapture editor over its local socket.

    tools/editor/cli.py help
    tools/editor/cli.py workspace list
    tools/editor/cli.py screenshot panel Outliner --workspace "World Editor"
    tools/editor/cli.py --complete "screenshot panel --"

The words are the command as the editor reads it. A word holding spaces is passed on in quotes.
"""

import glob
import os
import socket
import sys


def findSocket():
    folder = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    sockets = sorted(glob.glob(os.path.join(folder, "rapture-*.sock")))
    if not sockets:
        sys.exit("no running editor found in %s" % folder)
    if len(sockets) > 1:
        sys.exit("more than one editor is running, pick one with --socket:\n  " + "\n  ".join(sockets))
    return sockets[0]


def readExactly(connection, count):
    data = b""
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            sys.exit("the editor closed the connection")
        data += chunk
    return data


def readHeader(connection):
    header = b""
    while not header.endswith(b"\n"):
        chunk = connection.recv(1)
        if not chunk:
            sys.exit("the editor closed the connection")
        header += chunk
    outcome, size = header.decode().split()
    return outcome, int(size)


def quoteWord(word):
    if word and not any(c.isspace() or c == '"' for c in word):
        return word
    return '"' + word.replace("\\", "\\\\").replace('"', '\\"') + '"'


def run(connection, command):
    connection.sendall(command.encode() + b"\n")
    outcome, size = readHeader(connection)
    text = readExactly(connection, size).decode(errors="replace")
    return outcome == "ok", text


def main():
    # flags for this script come first, everything from the first other word on is the command
    arguments = sys.argv[1:]
    socketPath = None
    completeLine = None
    while arguments and arguments[0] in ("--socket", "--complete", "-h", "--help"):
        flag = arguments.pop(0)
        if flag in ("-h", "--help"):
            print(__doc__)
            return
        if not arguments:
            sys.exit(flag + " needs a value")
        if flag == "--socket":
            socketPath = arguments.pop(0)
        else:
            completeLine = arguments.pop(0)

    if completeLine is not None:
        command = "complete " + quoteWord(completeLine)
    else:
        command = " ".join(quoteWord(word) for word in arguments)
    if not command:
        sys.exit("give a command, help lists them")

    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.connect(socketPath or findSocket())

    succeeded, text = run(connection, command)
    stream = sys.stdout if succeeded else sys.stderr
    if text:
        print(text, end="" if text.endswith("\n") else "\n", file=stream)
    if not succeeded:
        sys.exit(1)


if __name__ == "__main__":
    main()
