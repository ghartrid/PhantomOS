# ==============================================================================
#                    PHANTOM ANTI-MALWARE SIGNATURE DATABASE
#                         "To Create, Not To Destroy"
# ==============================================================================
#
# Format: SHA256_HASH:THREAT_TYPE:THREAT_NAME
#
# Threat Types:
#   0 = Clean (shouldn't be in DB)
#   1 = Suspicious
#   2 = Malware (generic)
#   3 = Ransomware
#   4 = Trojan
#   5 = Virus
#   6 = Worm
#   7 = Rootkit
#   8 = Adware/PUP
#   9 = Spyware
#  10 = Cryptominer
#  11 = Backdoor/RAT
#
# This is a sample database with test signatures.
# For production use, integrate with ClamAV, VirusTotal, or similar sources.
#
# ==============================================================================

# EICAR Test File (industry-standard antivirus test)
# The EICAR test file is detected as malware for testing purposes
275a021bbfb6489e54d471899f7db9d1663fc695ec2fe2a2c4538aabf651fd0f:2:EICAR-Test-File
3419dbac7cbcf1b07bd33a675365ec5b799679d628af27e5df573a93a58a044b:2:EICAR-Test-File-Variant

# Known Linux Malware Hashes (examples - not real)
# These are placeholder hashes for demonstration
0000000000000000000000000000000000000000000000000000000000000001:4:Linux.Trojan.Mirai.A
0000000000000000000000000000000000000000000000000000000000000002:6:Linux.Worm.Tsunami.A
0000000000000000000000000000000000000000000000000000000000000003:7:Linux.Rootkit.Reptile.A
0000000000000000000000000000000000000000000000000000000000000004:11:Linux.Backdoor.Kaiten.A
0000000000000000000000000000000000000000000000000000000000000005:10:Linux.Cryptominer.XMRig.A

# Ransomware Examples
0000000000000000000000000000000000000000000000000000000000000010:3:Ransomware.Linux.HelloKitty.A
0000000000000000000000000000000000000000000000000000000000000011:3:Ransomware.Linux.Lilocked.A
0000000000000000000000000000000000000000000000000000000000000012:3:Ransomware.Linux.REvil.A

# Web Shell Signatures
0000000000000000000000000000000000000000000000000000000000000020:11:Webshell.PHP.C99.A
0000000000000000000000000000000000000000000000000000000000000021:11:Webshell.PHP.B374K.A
0000000000000000000000000000000000000000000000000000000000000022:11:Webshell.PHP.WSO.A

# Cryptominer Signatures
0000000000000000000000000000000000000000000000000000000000000030:10:Cryptominer.Linux.XMRig.A
0000000000000000000000000000000000000000000000000000000000000031:10:Cryptominer.Linux.CCMiner.A
0000000000000000000000000000000000000000000000000000000000000032:10:Cryptominer.Linux.SRBMiner.A

# Adware/PUP
0000000000000000000000000000000000000000000000000000000000000040:8:PUP.Linux.Adware.Generic
0000000000000000000000000000000000000000000000000000000000000041:8:PUP.Linux.Toolbar.Generic

# Spyware
0000000000000000000000000000000000000000000000000000000000000050:9:Spyware.Linux.Keylogger.A
0000000000000000000000000000000000000000000000000000000000000051:9:Spyware.Linux.Screengrab.A

# ==============================================================================
# END OF SIGNATURE DATABASE
# ==============================================================================
