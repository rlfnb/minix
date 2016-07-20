
struct smbus_hc {
        int devind;
	u16_t v_id;
        u16_t d_id;
        u32_t base;
        u32_t size;
        int irq;
        int irq_hook;
        char *regs;
};

/* Host Status Register
   All status bits are set by hardware and cleared by the software writing a one
   to the particular bit position. Writing a zero to any bit position has no affect 
    - HBSY: Host Busy (HBSY) - A '1' indicates that the SMBus host is running a
      command from the host interface. No SMB registers should be accessed while 
      this bit is set. 
      Exception: The BLOCK DATA REGISTER can be accessed when this bit is set ONLY
      when the SMB_CMD bits (in Host control register) are programmed for Block
      command or I2C Read command. This is necessary in order to check the DONE_STS
      bit.
    - INTR: Interrupt (INTR) - When set, this indicates that the source of the
      interrupt or SMI was the successful completion of its last command.
    - DEVERR: Device Error (DERR) - When set, this indicates that the source of the
      interrupt or SMI was due one of the following: Illegal Command Field 
      Unclaimed Cycle (host initiated) Host Device Time-out Error. CRC Error Write
      Protection Access Error (START bit will be cleared, Device Error will be set
      and Host Busy is never set because SMB Transaction never took place).
    - BERR: Bus Error (BERR) - When set, this indicates the source of the interrupt
      or SMI was a transaction collision.
    - FAILED: Failed (FAIL) - When set, this indicates that the source of the
      interrupt or SMI was a failed bus transaction. This is set in response to the
      KILL bit being set to terminate the host transaction.
    - SMB_ALERTB: the SOC sets this bit to a '1' to indicates source of the 
      interrupt or SMI# was the SMB_ALERTB signal. Software resets this bit by
      writing a 1 to this location.
    - IUS: In Use Status (IUS) - After a full PCI reset, a read to this bit returns
      a 0. After the first read, subsequent reads will return a 1. A write of a 1
      to this bit will reset the next read value to 0. Writing a 0 to this bit has
      no effect. Software can poll this bit until it reads a 0, and will then own
      the usage of the host controller. This bit has no other effect on the hard-
      ware, and is only used as semaphore among various independent software
      threads that may need to use the SMBus host
    - BDS: BYTE_DONE_STS (BDS) - This bit will be set to 1 when the host controller
      has received a byte (for Block Read commands) or if it has completed trans-
      mission of a byte (for Block Write commands) when the 32-byte buffer is not
      being used. Note that this bit will be set, even on the last byte of the
      transfer. Software clears the bit by writing a 1 to the bit position. This
      bit has no meaning for block transfers when the 32-byte buffer is enabled.
      Note: When the last byte of a block message is received, the host controller 
      will set this bit. However, it will not immediately set the INTR bit (bit 1
      in this register). When the interrupt handler clears the BYTE_DONE_STS bit,
      the message is considered complete, and the host controller will then set the
      INTR bit (and generate another interrupt). Thus, for a block message of n 
      bytes, the SMBus host will generate n+1 interrupts. The interrupt handler 
      needs to be implemented to handle these cases. */
#define SMB_MEM_HSTS_OFFSET		0x00
 #define SMB_MEM_HSTS_HBSY		0x01
 #define SMB_MEM_HSTS_INTR		0x02
 #define SMB_MEM_HSTS_DEVERR		0x04
 #define SMB_MEM_HSTS_BERR		0x08
 #define SMB_MEM_HSTS_FAILED		0x10
 #define SMB_MEM_HSTS_SMB_ALERTB	0x20
 #define SMB_MEM_HSTS_IUS		0x40
 #define SMB_MEM_HSTS_BDS		0x80

/* Host Control Register
   Note: A read to this register will clear the pointer in the 32-byte buffer.
    - INTREN: Enable the generation of an interrupt or SMI upon the completion of the
      command. Enables also other interrupt sendings, like ALERT and HOST_NOTIFY
    - KILL: KILL - When set, kills the current host transaction taking place, sets 
      the FAILED status bit, and asserts the interrupt (or SMI) selected by the
      SMB_INTRSEL field. This bit, once set, must be cleared to allow the SMB Host
      Controller to function normally
    - SMBCMD: SMB_CMD - As shown by the bit encoding below, indicates which command
      the SMBus host is to perform. If enabled, the SMBus host will generate an
      interrupt or SMI when the command has completed If the value is for a non-
      supported or reserved command, the SMBus host will set the device error
      (DEV_ERR) status bit and generate an interrupt when the START bit is set. The 
      SMBus controller will perform no command, and will not operate until DEV_ERR is
      cleared.
    - LBYTE: LAST_BYTE: Used for I2C Read commands as an indication that the next byte
      will be the last one to be received for that block. The algorithm and usage
      model for this bit will be as follows (assume a message of n bytes): 
      A. When the software sees the BYTE_DONE_STS bit set (bit 7 in the SMBus Host
         Status Register) for each of bytes 1 through n-2 of the message, the software
         should then read the Block Data Byte Register to get the byte that was just
         received.  
      B. After reading each of bytes 1 to n-2 of the message, the software will then
         clear the BYTE_DONE_STS bit. 
      C. After receiving byte n-1 of the message, the software will then set the 
         'LAST BYTE' bit. The software will then clear the BYTE_DONE_STS bit. 
      D. The Intel PCH will then receive the last byte of the message (byte n). However,
         the Intel PCH state machine will see the LAST BYTE bit set, and instead of
         sending an ACK after receiving the last byte, it will instead send a NAK. 
      E. After receiving the last byte (byte n), the software will still clear the
         BYTE_DONE_STS bit. However, the LAST_BYTE bit will be irrelevant at that point.
      Note: This bit may be set when the TCO timer causes the SECOND_TO_STS bit to be
      set. See the TCO2_STS Register in Volume 1, bit 1 for more details on that bit.
      The SMBus device driver should clear the LAST_BYTE bit (if it is set) before 
      starting any new command. Note: In addition to I2C Read Commands, the LAST_BYTE
      bit will also cause Block Read/ Write cycles to stop prematurely (at the end of
      the next byte).
    - START (SATRT): START: This write-only bit is used to initiate the command
      described in the SMB_CMD field. All registers should be setup prior to writing a 
      '1' to this bit position. This bit always reads zero. The HOST_BUSY bit in the 
      Host Status register (offset 00h) can be used to identify when the SMBus
      controller has finished the command.
    - PECEN: PEC_EN: When set to '1', this bit causes the host controller to perform the
      SMBus transaction with the Packet Error Checking phase appended. For writes, the
      value of the PEC byte is transferred from the PEC Register. For reads, the PEC
      byte is loaded in to the PEC Register. When this bit is cleared to '0', the SMBus 
      host controller does not perform the transaction with the PEC phase appended. This
      bit must be written prior to the write in which the START bit is set. */
#define SMB_MEM_HCTL_OFFSET		0x02
 #define SMB_MEM_HCTL_INTREN		0x01
 #define SMB_MEM_HCTL_KILL		0x02
 /* #define SMB_MEM_HCTL_SMBCMD		3 bit wide   SMB CMD */
  #define SMB_MEM_HCTL_SMBCMD_QUICK	0x00	/* The slave address and read/write value (bit 0) are stored in the tx slave address register */
  #define SMB_MEM_HCTL_SMBCMD_BYTE	0x01	/* This command uses the transmit slave address and command registers. Bit 0 of the slave address register determines if this is a read or write command. If it is a read, after the command completes the DATA0 register will contain the read data. */
  #define SMB_MEM_HCTL_SMBCMD_BYTE_DATA	0x02 /* This command uses the transmit slave address, command, and DATA0 registers. Bit 0 of the slave address register determines if this is a read or write command. If it is a read, the DATA0 register will contain the read data */
  #define SMB_MEM_HCTL_SMBCMD_WORD_DATA	0x03	/* This command uses the transmit slave address, command, DATA0 and DATA1 registers. Bit 0 of the slave address register determines if this is a read or write command. If it is a read, after the command completes the DATA0 and DATA1 registers will contain the read data. */
  #define SMB_MEM_HCTL_SMBCMD_PRCS_CALL	0x04	/* This command uses the transmit slave address, command, DATA0 and DATA1 registers. Bit 0 of the slave address register determines if this is a read or write command. After the command completes, the DATA0 and DATA1 registers will contain the read data. */
  #define SMB_MEM_HCTL_SMBCMD_BLOCK	0x05	/* This command uses the transmit slave address, command, and DATA0 registers, and the Block Data Byte register. For block write, the count is stored in the DATA0 register and indicates how many bytes of data will be transferred. For block reads, the count is received and stored in the DATA0 register. Bit 0 of the slave address register selects if this is a read or write command. For writes, data is retrieved from the first n (where n is equal to the specified count) addresses of the SRAM array. For reads, the data is stored in the Block Data Byte register.*/
  #define SMB_MEM_HCTL_SMBCMD_I2C_READ	0x06	/* This command uses the transmit slave address, command, DATA0, DATA1 registers, and the Block Data Byte register. The read data is stored in the Block Data Byte register. The Intel PCH will continue reading data until the NAK is received. */
  #define SMB_MEM_HCTL_SMBCMD_BLK_PRCS	0x07	/* This command uses the transmit slave address, command, DATA0 and the Block Data Byte register. For block write, the count is stored in the DATA0 register and indicates how many bytes of data will be transferred. For block read, the count is received and stored in the DATA0 register. Bit 0 of the slave address register always indicate a write command. For writes, data is retrieved from the first m (where m is equal to the specified count) addresses of the SRAM array. For reads, the data is stored in the Block Data Byte register. Note: E32B bit in the Auxiliary Control Register must be set for this command to work. */
 #define SMB_MEM_HCTL_LBYTE		0x20
 #define SMB_MEM_HCTL_START		0x40
 #define SMB_MEM_HCTL_PECEN		0x80

/* This eight bit field is transmitted by the host controller in the command field
   of the SMB protocol during the execution of any command. 
*/
#define SMB_MEM_HCMD_OFFSET		0x03

#define SMB_MEM_TSA_OFFSET		0x04	/* This register is transmitted by the host controller in the slave address field of the SMB protocol. This is the address of the target */

#define SMB_MEM_HD0_OFFSET		0x05	/* Data 0 Register */

#define SMB_MEM_HD1_OFFSET		0x06	/* Data 1 Register */

#define SMB_MEM_HBD_OFFSET		0x07	/* Host Block Data */

#define SMB_MEM_PEC_OFFSET		0x08	/* This register contains the 8-bit CRC value that is used as the Packet Error Check on SMBus. For writes, this register is written by software prior to running the command. For reads, this register is read by software after the read command is completed on SMBus. */ 

#define SMB_MEM_AUXS_OFFSET		0x0c	/* Auxiliary Status */

#define SMB_MEM_AUXC_OFFSET		0x0d	/* Auxiliary Control */

#define SMB_MEM_SMBC_OFFSET		0x0f	/* SMBUS_PIN_CTL Register */
 #define SMB_MEM_SMBC_SMBCLK		0x01	/* This pin returns the value on the SMB_CLK pin. It will be 1 to indicate high, 0 to indicate low. This allows software to read the current state of the pin. */
 #define SMB_MEM_SMBC_SMBDAT		0x02	/* This pin returns the value on the SMB_DATA pin. It will be 1 to indicate high, 0 to indicate low. This allows software to read the current state of the pin.*/ 
 #define SMB_MEM_SMBC_SMBCLKCTL		0x04	/* This Read/Write bit has a default of 1. 0 = SMBus controller will drive the SMB_CLK pin low, independent of what the other SMB logic would otherwise indicate for the SMB_CLK pin. 1 = The SMB_CLK pin is Not overdriven low. The other SMBus logic controls the state of the pin. */

#define SMB_MEM_SSTS_OFFSET		0x10	/* Slave Status Register */
 #define SMB_MEM_SSTS_HNST		0x01	/* HOST_NOTIFY_STS: The SMBus controller sets this bit to a 1 when it has completely received a successful Host Notify Command on the SMBus pins. Software reads this bit to determine that the source of the interrupt or SMI# was the reception of the Host Notify Command. Software clears this bit after reading any information needed from the Notify address and data registers by writing a 1 to this bit. Note that the SMBus controller will allow the Notify Address and Data registers to be over-written once this bit has been cleared. When this bit is 1, the SMBus controller will NACK the first byte (host address) of any new 'Host Notify' commands on the SMBus. Writing a 0 to this bit has no effect. */

#define SMB_MEM_SCMD_OFFSET		0x11	/* Slave Command Register */

#define SMB_MEM_NDA_OFFSET		0x14	/* Notify Device Address Register */

#define SMB_MEM_NDLB_OFFSET		0x16	/* Notify Data Low Byte Register */

#define SMB_MEM_NDHB_OFFSET		0x17	/* Notify Data High Byte Register */



