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
        'concepts/threads-and-processes',
        'concepts/context-switching',
      ],
    },
    {
      type: 'category',
      label: 'Project Guides',
      items: [
        'projects/threads/overview',
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
