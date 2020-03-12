import datetime


class Logger:
    def __init__(self):
        self.debug_mode = False
        super().__init__()

    def turn_debug_on(self):
        self.debug_mode = True

    def debug_print(self, message):
        now = datetime.datetime.now()
        if self.debug_mode:
            print("{0}:{1}:{2} => {3}".format(now.hour, now.minute, now.second, message))
