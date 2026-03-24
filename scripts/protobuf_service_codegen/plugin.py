from __future__ import annotations

import sys

from google.protobuf.compiler import plugin_pb2

from .generator import ProtobufRpcCppGenerator


def main() -> int:
    request = plugin_pb2.CodeGeneratorRequest()
    request.ParseFromString(sys.stdin.buffer.read())

    generator = ProtobufRpcCppGenerator()
    response = generator.generate(request)
    sys.stdout.buffer.write(response.SerializeToString())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
