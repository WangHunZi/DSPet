"""网关、命令行和 HTTP 共用的错误。"""


class GatewayError(Exception):
    def __init__(self, code, status, message):
        super().__init__(message)
        self.code = code
        self.status = status
