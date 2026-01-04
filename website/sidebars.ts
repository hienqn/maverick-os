import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  tutorialSidebar: [
    'intro',
    {
      type: 'category',
      label: 'Getting Started',
      items: [
        'getting-started/installation',
        'getting-started/first-run',
        'getting-started/debugging',
        'getting-started/contributing',
      ],
    },
    {
      type: 'category',
      label: 'Architecture',
      items: [
        'architecture/overview',
        'architecture/memory-layout',
        'architecture/source-tree',
      ],
    },
    {
      type: 'category',
      label: 'OS Concepts',
      collapsed: false,
      items: [
        'concepts/threads-and-processes',
        'concepts/context-switching',
        'concepts/synchronization',
        'concepts/scheduling',
        'concepts/priority-donation',
        'concepts/system-calls',
        'concepts/virtual-memory',
        'concepts/demand-paging',
        'concepts/filesystem-basics',
      ],
    },
    {
      type: 'category',
      label: 'Project Guides',
      items: [
        {
          type: 'category',
          label: 'Project 1: Threads',
          items: [
            'projects/threads/overview',
            'projects/threads/alarm-clock',
            'projects/threads/priority-scheduler',
            'projects/threads/mlfqs',
          ],
        },
        {
          type: 'category',
          label: 'Project 2: User Programs',
          items: [
            'projects/userprog/overview',
            'projects/userprog/argument-passing',
            'projects/userprog/system-calls',
            'projects/userprog/process-management',
          ],
        },
        {
          type: 'category',
          label: 'Project 3: Virtual Memory',
          items: [
            'projects/vm/overview',
            'projects/vm/page-table',
            'projects/vm/frame-table',
            'projects/vm/swap',
            'projects/vm/cow-fork',
          ],
        },
        {
          type: 'category',
          label: 'Project 4: File System',
          items: [
            'projects/filesys/overview',
            'projects/filesys/buffer-cache',
            'projects/filesys/extensible-files',
            'projects/filesys/subdirectories',
            'projects/filesys/wal',
          ],
        },
      ],
    },
    {
      type: 'category',
      label: 'Deep Dives',
      items: [
        'deep-dives/context-switch-assembly',
        'deep-dives/page-fault-handler',
        'deep-dives/clock-algorithm',
        'deep-dives/buffer-cache',
        'deep-dives/wal',
      ],
    },
    {
      type: 'category',
      label: 'Roadmap',
      items: [
        'roadmap/changelog',
        'roadmap/completed-features',
        'roadmap/future-smp',
      ],
    },
  ],
};

export default sidebars;
