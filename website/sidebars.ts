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
      items: [
        'concepts/index',
        'concepts/threads-and-processes',
        'concepts/context-switching',
        'concepts/system-calls',
        'concepts/virtual-memory',
        'concepts/scheduling',
        'concepts/priority-donation',
      ],
    },
    {
      type: 'category',
      label: 'Project Guides',
      items: [
        'projects/threads/overview',
        'projects/userprog/overview',
        'projects/vm/overview',
        'projects/filesys/overview',
      ],
    },
    {
      type: 'category',
      label: 'Deep Dives',
      items: [
        'deep-dives/context-switch-assembly',
        'deep-dives/page-fault-handling',
        'deep-dives/wal-crash-recovery',
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
