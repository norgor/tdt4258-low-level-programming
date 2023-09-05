.global _start


// Please keep the _start method and the input strings name ("input") as
// specified below
// For the rest, you are free to add and remove functions as you like,
// just make sure your code is clear, concise and well documented.

_start:
	// Load input and call is_palindrome
	ldr r0, =input
	bl is_palindrome
	// Check return value and notify that it was, or was not, a palindrome
	teq r0, #0
	bleq palindrome_not_ok
	blne palindrome_ok
	
	// Jump to exit
	b _exit

// Returns the string length (in r0) of the text pointed to by r0.
strlen:
	eor r1, r1 // Clear r1
strlen_loop:
	ldrb r2, [r0] // Load char from string into r2
	teq r2, #0 // Have we reached NUL?
	beq strlen_ret // If so, return.
	
	add r1, #1 // Increment length
	add r0, #1 // Increment string pointer
	b strlen_loop // Loop again
strlen_ret:
	mov r0, r1 // Move string length to r0
	mov pc, lr // Return to caller
	
// Makes the character in r0 lowercase.
to_upper:
	// Return if r0 <= 'a'
	cmp r0, #0x61
	blt to_upper_ret // Jump out if r0 <= 'a'
	// Return if r0 >= 'z'
	cmp r0, #0x7A // Compare to 'z' 
	bgt to_upper_ret // Jump out if r0 >= 'z'
	// Make r0 uppercase
	sub r0, #0x20 // Make the character uppercase.
to_upper_ret:
	mov pc, lr // Return to caller
	
// Compares the characters in register r0 and r1 case insensitively.
// r0 = 0 if chars were equal, else, r0 != 0.
cmpchar_nocase:
	push {lr} // Save lr
	// Make r0 lowercase
	bl to_upper
	// Make r1 lowercase
	push {r0} // Save r0
	mov r0, r1 // Move r1 char to r0
	bl to_upper // Make r1 uppercase
	mov r1, r0 // Move r0 char back into r1
	pop {r0} // Restore r0
	// Compare
	sub r0, r1
	// Return
	pop {lr} // Pop lr
	mov pc, lr // Return to caller
	
	
// Checks if the string in r0 is a palindrome.
// Returns r0=1 if true, else, r0!=0.
is_palindrome:
	push {r4, r5, lr} // Save registers
	mov r4, r0 // Move string pointer into r4
	// Determine string length
	bl strlen // Get length of string
	// Set up pointers: r4=start, r5=end
	add r5, r4, r0 // Set end pointer to string pointer + strlen
	sub r4, #1 // Point before start of buffer, as we add in loop.
is_palindrome_loop:
	// Compare pointer positions (end - start) <= 0 means palindrome, else we must continue.
	sub r0, r5, r4
	cmp r0, #0
	ble is_palindrome_true // Return true! :)
	// Read chars into r0 and r1
read_start:
	add r4, #1
	ldrb r0, [r4]
	// Read next char if space
	teq r0, #0x20
	beq read_start
read_end:
	sub r5, #1
	ldrb r1, [r5]
	// Read next char if space
	teq r1, #0x20
	beq read_end
	
	// Are the chars equal?
	bl cmpchar_nocase
	teq r0, #0
	bne is_palindrome_false // Return false.
	b is_palindrome_loop
is_palindrome_true:
	mov r0, #1
	b is_palindrome_ret
is_palindrome_false:
	mov r0, #0
is_palindrome_ret:
	pop {r4, r5, lr} // Restore registers
	mov pc, lr // Return to caller

// Prints the string pointed to by r0 into the JTAG UART port.
print:
	ldr r2, =#JTAG
	ldrb r1, [r0] // Read character
	teq r1, #0 // Return if NUL
	moveq pc, lr
	// Write char to UART
	strb r1, [r2]
	add r0, #1
	b print
	
// Notifies that the palindrome was OK.
palindrome_ok:
	push {lr}
	
	// Switch on only the 5 leftmost LEDs
	ldr r0, =#LEDR
	mov r1, #0b1111100000
	str r1, [r0]
	// Write 'Palindrom detected' to UART
	ldr r0, =palindrome
	bl print
	
	pop {lr}
	mov pc, lr

// Notifies that palindrome was NOT OK.
palindrome_not_ok:
	push {lr}
	
	// Switch on only the 5 rightmost LEDs
	ldr r0, =#LEDR
	mov r1, #0b11111
	str r1, [r0]
	// Write 'Not a palindrom' to UART
	ldr r0, =notPalindrome
	bl print
	
	pop {lr}
	mov pc, lr
	
_exit:
	// Branch here for exit
	b .

.equ LEDR, 0xFF200000
.equ JTAG, 0xFF201000

.data
.align
	// This is the input you are supposed to check for a palindrom
	// You can modify the string during development, however you
	// are not allowed to change the name 'input'!
	input: .asciz "pa lin dromeemordnilapx"
	palindrome: .asciz "Palindrome detected"
	notPalindrome: .asciz "Not a palindrome"
.end
