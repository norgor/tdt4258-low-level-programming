from os import listdir
from os.path import splitext, join

TESTCASE_DIR = "testcases"

class Test:
    def __init__(self, name, args, accesses, hits) -> None:
        self.name = name;
        self.args = args;
        self.accesses = accesses;
        self.hits = hits;
        


def get_tests():
    names = set(map(lambda x: splitext(x)[0], listdir(TESTCASE_DIR)))
    sorted_tests = sorted(names)
    tests = list()
    for name in names:
        input = join(TESTCASE_DIR, name) + ".txt"
        out = join(TESTCASE_DIR, name) + ".out"
        with open(out) as of:
            of.read()
    return tests
        

def run_test(test):
    print(f"running test {test}") 

def main():
    tests = get_tests()
    for test in tests:
        run_test(test)
    

if __name__ == "__main__":
    main();
