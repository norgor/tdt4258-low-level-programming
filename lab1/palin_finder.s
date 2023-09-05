.global _start

_start:
	// Load input and call is_palindrome
	ldr r0, =input // Set r0 to pointer to input string.
	bl is_palindrome // Check if is palindrome.
	teq r0, #0 // Is false?
	bleq palindrome_not_ok // Yes => Notify IS NOT a palindrome.
	blne palindrome_ok // No => Notify IS a palindrome.
	
	b _exit	// Jump to exit

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
	cmp r0, #0x61 // Compare char to 'a'
	blt to_upper_ret // Return if char <= 'a'.
	// Return if r0 >= 'z'
	cmp r0, #0x7A // Compare char to 'z' 
	bgt to_upper_ret // Return if char >= 'z'
	
	sub r0, #0x20 // Make the character uppercase.
to_upper_ret:
	mov pc, lr // Return to caller
	
// Compares the characters in register r0 and r1 case insensitively.
// r0 = 0 if chars were equal, else, r0 != 0.
cmpchar_nocase:
	push {lr} // Save lr, as we perform calls inside this function.
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
	pop {pc} // Return to caller
	
	
// Checks if the string in r0 is a palindrome.
// Returns r0=1 if true, else, r0!=0.
is_palindrome:
	push {r4, r5, lr} // Save registers
	mov r4, r0 // Move string pointer into r4
	// Determine string length
	bl strlen // Get length of string
	// Set up pointers: r4=start, r5=end
	add r5, r4, r0 // Set end pointer to string pointer + strlen
	sub r4, #1 // Point before start of buffer, as we add in loop
is_palindrome_loop:
	// Bounds check
	// Compare pointer positions (end - start) <= 0 means palindrome, else we must continue
	sub r0, r5, r4 // delta = end - start
	cmp r0, #0  // compare delta to 0
	ble is_palindrome_true // delta is <= 0 (palindrome). return true!
	// Read chars into r0 and r1
read_start:
	add r4, #1 // Offset start by 1
	ldrb r0, [r4] // Read start character
	teq r0, #0x20 // Is character space?
	beq read_start // Jump back and read another start character if it was a space
read_end:
	sub r5, #1 // Offset end by -1
	ldrb r1, [r5] // Read end character
	teq r1, #0x20 // Is character space?
	beq read_end // Jump back and read another end chacter if it was a space
	
	// Compare start and end
	bl cmpchar_nocase // Compare the start and end character
	teq r0, #0 // Compare return value to 0
	bne is_palindrome_false // Return false if characters inequal (return value != 0)
	b is_palindrome_loop // Perform another loop iteration.
is_palindrome_true:
	mov r0, #1 // Set return value to 1.
	b is_palindrome_ret // Jump to return
is_palindrome_false:
	mov r0, #0 // Set return value to 0.
is_palindrome_ret:
	pop {r4, r5, pc} // Restore registers

// Prints the string pointed to by r0 into the JTAG UART port.
print:
	ldr r2, =#JTAG // Set r2 to the JTAG base address.
	ldrb r1, [r0] // Read character
	teq r1, #0 // Compare character to NUL.
	moveq pc, lr // Return if character was NUL.
	// Write char to UART
	strb r1, [r2] // Write character into JTAG UART address.
	add r0, #1 // Increment string pointer by one.
	b print // Loop again.
	
// Notifies that the palindrome was OK.
palindrome_ok:
	push {lr} // Store lr, as we perform function calls
	
	// Switch on only the 5 leftmost LEDs
	ldr r0, =#LEDR // Store LEDR base address into r0
	mov r1, #0b1111100000 // Set the 5 leftmost LED bits
	str r1, [r0] // Write LED bits into LED peripheral register
	
	// Write 'Palindrom detected' to UART
	ldr r0, =palindrome // Set r0 to pointer to palindrome string
	bl print // Call print
	
	pop {pc} // Return to caller

// Notifies that palindrome was NOT OK.
palindrome_not_ok:
	push {lr}
	
	// Switch on only the 5 rightmost LEDs
	ldr r0, =#LEDR // Store LEDR base address into r0
	mov r1, #0b11111 // Set the 5 rightmost LED bits
	str r1, [r0]// Write LED bits into LED peripheral register
	
	// Write 'Not a palindrom' to UART
	ldr r0, =notPalindrome // Set r0 to pointer to not a palindrome string
	bl print // Call print
	
	pop {pc} // Return to caller
	
_exit:
	// Branch here for exit
	b .

.equ LEDR, 0xFF200000 // LEDR base address.
.equ JTAG, 0xFF201000 // JTAG base address.

.data
.align
	// This is the input you are supposed to check for a palindrom
	// You can modify the string during development, however you
	// are not allowed to change the name 'input'!
	input: .asciz "levelx"
	
	palindrome: .asciz "Palindrome detected"
	notPalindrome: .asciz "Not a palindrome"
.end
