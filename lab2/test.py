from os import listdir
from os.path import splitext, join
import subprocess

TESTCASE_DIR = "testcases"

class Test:
    def __init__(self, name, args, accesses, hits) -> None:
        self.name = name;
        self.args = args;
        self.accesses = accesses;
        self.hits = hits;
        


def get_tests():
    names = set(map(lambda x: splitext(x)[0], listdir(TESTCASE_DIR)))
    sorted_names = sorted(names)
    tests = list()
    for name in sorted_names:
        input = join(TESTCASE_DIR, name) + ".txt"
        out = join(TESTCASE_DIR, name) + ".out"
        args = None
        accesses = None
        hits = None
        i = 1
        with open(out) as of:
            for line in of:
                if line.startswith("#>"): 
                    frags = line.strip("#> ").split(" ")
                    args = (frags[1], frags[2], frags[3], input)
                elif line.startswith("Accesses:"):
                    accesses = int(line.split(":")[1])
                elif line.startswith("Hits:"):
                    hits = int(line.split(":")[1])
                    tests.append(Test(f"{name} #{i}", args, accesses, hits))
                    i += 1
            tests.append(Test(f"{name} #{i}", args, accesses, hits))

    return tests
        

def run_test(test: Test):
    print(f"running test {test.name}...", end = "")
    cmd = subprocess.run(("./cache_sim", *test.args), capture_output=True, check=True)
    accesses = None
    hits = None
    for line in cmd.stdout.splitlines():
        if line.startswith(b"Accesses:"):
            accesses = int(line.split(b":")[1])
        elif line.startswith(b"Hits:"):
            hits = int(line.split(b":")[1])
    
    error = ""
    if test.hits != hits:
        error += f"hits {hits}, want {test.hits}\n"
    if test.accesses != accesses:
        error += f"accesses {accesses}, want {test.accesses}\n"
    if error:
        print(f" ERROR\n{error}")
    else:
        print(" OK")




def main():
    tests = get_tests()
    for test in tests:
        run_test(test)
    

if __name__ == "__main__":
    main();
