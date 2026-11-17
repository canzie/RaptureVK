#!/usr/bin/env python3
"""Issue commands to a running Rapture editor over its local socket.

    tools/editor/cli.py 'workspace.list()'
    tools/editor/cli.py 'screenshot.panel("Outliner", "/tmp/outliner.png")'
    tools/editor/cli.py 'help()'

Several commands run one after another, stopping at the first that fails.
"""

import argparse
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


def run(connection, command):
    connection.sendall(command.encode() + b"\n")
    outcome, size = readHeader(connection)
    text = readExactly(connection, size).decode(errors="replace")
    return outcome == "ok", text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("commands", nargs="+", help="command calls, for example 'workspace.list()'")
    parser.add_argument("--socket", help="the editor's socket, found automatically when one editor is running")
    arguments = parser.parse_args()

    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.connect(arguments.socket or findSocket())

    for command in arguments.commands:
        succeeded, text = run(connection, command)
        stream = sys.stdout if succeeded else sys.stderr
        if text:
            print(text, end="" if text.endswith("\n") else "\n", file=stream)
        if not succeeded:
            sys.exit(1)


if __name__ == "__main__":
    main()
