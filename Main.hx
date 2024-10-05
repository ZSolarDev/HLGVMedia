package;

import hlmp4.HLMP4.Greetings;

class Main {
	static public function main() {
        while (true){
		    trace('Result managed in C side / greeting: ' + Greetings.get_greeting());
        }
	}
}
