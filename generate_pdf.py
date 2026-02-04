#!/usr/bin/env python3
"""Generate PDF from PhantomOS Implementation Summary"""

from fpdf import FPDF

class PDF(FPDF):
    def header(self):
        self.set_font('Helvetica', 'B', 10)
        self.cell(0, 10, 'PhantomOS Implementation Summary', 0, 1, 'C')
        self.ln(2)

    def footer(self):
        self.set_y(-15)
        self.set_font('Helvetica', 'I', 8)
        self.cell(0, 10, f'Page {self.page_no()}', 0, 0, 'C')

    def chapter_title(self, title):
        self.set_font('Helvetica', 'B', 14)
        self.set_fill_color(200, 220, 255)
        self.cell(0, 10, title, 0, 1, 'L', True)
        self.ln(2)

    def section_title(self, title):
        self.set_font('Helvetica', 'B', 11)
        self.cell(0, 8, title, 0, 1, 'L')
        self.ln(1)

    def body_text(self, text):
        self.set_font('Helvetica', '', 10)
        self.multi_cell(0, 5, text)
        self.ln(2)

    def code_block(self, text):
        self.set_font('Courier', '', 9)
        self.set_fill_color(240, 240, 240)
        self.multi_cell(0, 4, text, 0, 'L', True)
        self.ln(2)

    def table_row(self, cols, widths, header=False):
        self.set_font('Helvetica', 'B' if header else '', 9)
        for i, col in enumerate(cols):
            self.cell(widths[i], 6, col, 1, 0, 'L')
        self.ln()

# Create PDF
pdf = PDF()
pdf.set_auto_page_break(auto=True, margin=15)
pdf.add_page()

# Title
pdf.set_font('Helvetica', 'B', 24)
pdf.cell(0, 20, 'PhantomOS', 0, 1, 'C')
pdf.set_font('Helvetica', 'B', 16)
pdf.cell(0, 10, 'Implementation Summary', 0, 1, 'C')
pdf.set_font('Helvetica', 'I', 12)
pdf.cell(0, 10, '"To Create, Not To Destroy"', 0, 1, 'C')
pdf.ln(10)

# Philosophy
pdf.chapter_title('Core Philosophy')
pdf.body_text(
    'PhantomOS is an operating system where destruction is architecturally impossible. '
    'There are no rm, kill, or delete commands. Data is hidden, not deleted. '
    'Processes become dormant, not terminated. All history is preserved in the geological record.'
)

# GeoFS
pdf.chapter_title('1. GeoFS (Geology FileSystem)')
pdf.body_text('Files: geofs.c, geofs.h')
pdf.section_title('Key Features:')
pdf.body_text(
    '- Content-addressed storage: Files stored by SHA-256 hash\n'
    '- Append-only: Data is never overwritten, only new versions created\n'
    '- Views: Time-travel through storage history (like geological layers)\n'
    '- References: Paths map to content hashes\n'
    '- Hide, not delete: Content remains in geology, just hidden from view'
)
pdf.section_title('API:')
pdf.code_block(
    'geofs_volume_create/open/close()   // Volume lifecycle\n'
    'geofs_content_store/read()         // Content operations\n'
    'geofs_ref_create/resolve/list()    // Path references\n'
    'geofs_view_create/hide()           // View management'
)

# VFS
pdf.chapter_title('2. VFS (Virtual File System)')
pdf.body_text('Files: kernel/vfs.c, kernel/vfs.h')
pdf.body_text(
    'POSIX-like VFS layer that routes operations to different filesystem backends. '
    'Supports multiple filesystems (procfs, devfs, geofs), file descriptors, '
    'directory operations, symlinks, and hide() instead of unlink().'
)
pdf.section_title('Mounted Filesystems:')
pdf.table_row(['Mount Point', 'Type', 'Description'], [40, 30, 120], header=True)
pdf.table_row(['/proc', 'procfs', 'Process information'], [40, 30, 120])
pdf.table_row(['/dev', 'devfs', 'Device files'], [40, 30, 120])
pdf.table_row(['/geo', 'geofs', 'Persistent storage'], [40, 30, 120])
pdf.ln(4)

# Procfs
pdf.chapter_title('3. Procfs (Process Filesystem)')
pdf.body_text('File: kernel/procfs.c')
pdf.body_text(
    'Virtual filesystem exposing process and kernel information:\n'
    '/proc/version, /proc/uptime, /proc/stat, /proc/constitution, /proc/mounts, /proc/{pid}/status'
)

# Devfs
pdf.chapter_title('4. Devfs (Device Filesystem)')
pdf.body_text('File: kernel/devfs.c')
pdf.section_title('Devices:')
pdf.table_row(['Device', 'Description'], [50, 140], header=True)
pdf.table_row(['/dev/null', 'Discards writes, returns EOF on read'], [50, 140])
pdf.table_row(['/dev/zero', 'Returns infinite zeros'], [50, 140])
pdf.table_row(['/dev/random', 'Returns random bytes'], [50, 140])
pdf.table_row(['/dev/console', 'Terminal output'], [50, 140])
pdf.ln(4)

# Kernel
pdf.add_page()
pdf.chapter_title('5. Phantom Kernel')
pdf.body_text('Files: kernel/phantom.c, kernel/phantom.h')
pdf.section_title('Process Management:')
pdf.body_text(
    '- States: embryo -> ready -> running -> blocked -> dormant (never terminated)\n'
    '- No kill(): Only phantom_process_suspend() and phantom_process_resume()\n'
    '- Persistence: Processes saved to GeoFS, survive kernel restarts'
)
pdf.section_title('IPC:')
pdf.body_text(
    'Message passing with mailboxes, stored in GeoFS for persistence. '
    'Types: DATA, SIGNAL, REQUEST, RESPONSE'
)
pdf.section_title('Scheduler:')
pdf.body_text(
    'Round-robin, priority, or fair-share modes. Priority levels 0-31. 10ms default time slice.'
)

# Init System
pdf.chapter_title('6. Init System')
pdf.body_text('Files: kernel/init.c, kernel/init.h')
pdf.section_title('Service States:')
pdf.table_row(['State', 'Description'], [50, 140], header=True)
pdf.table_row(['SERVICE_EMBRYO', 'Being created'], [50, 140])
pdf.table_row(['SERVICE_STARTING', 'Dependencies resolving'], [50, 140])
pdf.table_row(['SERVICE_RUNNING', 'Active and healthy'], [50, 140])
pdf.table_row(['SERVICE_DORMANT', 'Inactive but preserved'], [50, 140])
pdf.table_row(['SERVICE_AWAKENING', 'Transitioning to running'], [50, 140])
pdf.ln(4)
pdf.section_title('Built-in Services:')
pdf.body_text('geofs, vfs, procfs, devfs, governor, shell')

# Governor
pdf.add_page()
pdf.chapter_title('7. Governor (AI Code Evaluator)')
pdf.body_text('Files: kernel/governor.c, kernel/governor.h')
pdf.body_text('Capability-based + interactive code evaluation system.')
pdf.section_title('Capabilities (16 types):')
pdf.body_text(
    'Files: read_files, write_files, create_files, hide_files\n'
    'Process: create_process, ipc_send, ipc_receive\n'
    'Memory: alloc_memory, high_memory\n'
    'Elevated: network, system_config, raw_device, high_priority\n'
    'Info: read_procfs, read_devfs\n'
    'Special: governor_bypass'
)
pdf.section_title('Threat Levels:')
pdf.table_row(['Level', 'Action'], [40, 150], header=True)
pdf.table_row(['NONE', 'Auto-approve'], [40, 150])
pdf.table_row(['LOW', 'Auto-approve with logging'], [40, 150])
pdf.table_row(['MEDIUM', 'Prompt user (interactive mode)'], [40, 150])
pdf.table_row(['HIGH', 'Prompt user or decline (strict mode)'], [40, 150])
pdf.table_row(['CRITICAL', 'Always decline (destructive code)'], [40, 150])
pdf.ln(4)
pdf.section_title('Destructive Patterns (~50):')
pdf.body_text(
    'File: unlink, remove, rmdir, truncate\n'
    'Process: kill(, abort, exit(\n'
    'System: reboot, shutdown, format\n'
    'Database: DROP TABLE, DELETE FROM\n'
    'Shell: rm -, >/dev/'
)

# Shell
pdf.chapter_title('8. Shell')
pdf.body_text('Files: kernel/shell.c, kernel/shell.h')
pdf.section_title('Commands:')
pdf.body_text(
    'Navigation: pwd, cd, ls, stat\n'
    'File Operations: cat, echo, mkdir, touch, write, ln -s\n'
    'Phantom-Specific: hide (preserved in geology)\n'
    'Process: ps, suspend, resume\n'
    'System: constitution, geology, mount\n'
    'Services: service list/status/awaken/rest\n'
    'Governor: governor status/stats/mode/test'
)
pdf.section_title('Notable Absences:')
pdf.body_text('No rm, rmdir, unlink, kill, killall, delete, destroy')

# Architecture
pdf.add_page()
pdf.chapter_title('Architecture Diagram')
pdf.code_block(
    '+------------------------------------------------------------+\n'
    '|                     PHANTOM SHELL                          |\n'
    '|              (service, governor, hide, etc.)               |\n'
    '+------------------------------------------------------------+\n'
    '|                    INIT SYSTEM                             |\n'
    '|        (Services: geofs, vfs, procfs, devfs, governor)     |\n'
    '+------------------------------------------------------------+\n'
    '|                     GOVERNOR                               |\n'
    '|     (Capability-based + Interactive Code Evaluation)       |\n'
    '+------------------------------------------------------------+\n'
    '|                       VFS                                  |\n'
    '|            procfs (/proc)  devfs (/dev)  geofs (/geo)      |\n'
    '+------------------------------------------------------------+\n'
    '|                  PHANTOM KERNEL                            |\n'
    '|     (Process, IPC, Memory, Scheduler - No destruction)     |\n'
    '+------------------------------------------------------------+\n'
    '|                     GeoFS                                  |\n'
    '|        (Append-only, Content-addressed Storage)            |\n'
    '+------------------------------------------------------------+'
)

# File Summary
pdf.chapter_title('File Summary')
pdf.table_row(['File', 'Lines', 'Description'], [60, 25, 105], header=True)
pdf.table_row(['geofs.c', '~1100', 'Append-only storage'], [60, 25, 105])
pdf.table_row(['kernel/phantom.c', '~1600', 'Kernel core'], [60, 25, 105])
pdf.table_row(['kernel/vfs.c', '~900', 'Virtual filesystem'], [60, 25, 105])
pdf.table_row(['kernel/shell.c', '~1500', 'Interactive shell'], [60, 25, 105])
pdf.table_row(['kernel/init.c', '~800', 'Init/service manager'], [60, 25, 105])
pdf.table_row(['kernel/governor.c', '~750', 'AI code evaluator'], [60, 25, 105])
pdf.table_row(['kernel/geofs_vfs.c', '~600', 'GeoFS-VFS adapter'], [60, 25, 105])
pdf.table_row(['kernel/procfs.c', '~400', 'Process filesystem'], [60, 25, 105])
pdf.table_row(['kernel/devfs.c', '~350', 'Device filesystem'], [60, 25, 105])
pdf.ln(4)
pdf.body_text('Total: ~9,500 lines of C code')

# Footer
pdf.ln(10)
pdf.set_font('Helvetica', 'I', 10)
pdf.cell(0, 10, 'Generated: January 2026', 0, 1, 'C')
pdf.cell(0, 5, 'PhantomOS - "To Create, Not To Destroy"', 0, 1, 'C')

# Save
pdf.output('/home/graham/Desktop/phantomos/PhantomOS_Implementation_Summary.pdf')
print('PDF generated: PhantomOS_Implementation_Summary.pdf')
