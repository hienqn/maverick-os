import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'PintOS Documentation',
  tagline: 'Learn Operating Systems Through Real Code',
  favicon: 'img/favicon.ico',

  url: 'https://your-org.github.io',
  baseUrl: '/group0/',

  organizationName: 'your-org',
  projectName: 'group0',

  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  markdown: {
    mermaid: true,
  },

  themes: ['@docusaurus/theme-mermaid'],

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          editUrl: 'https://github.com/your-org/group0/tree/main/website/',
          showLastUpdateTime: true,
          showLastUpdateAuthor: true,
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/pintos-social-card.png',
    navbar: {
      title: 'PintOS',
      logo: {
        alt: 'PintOS Logo',
        src: 'img/logo.svg',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'tutorialSidebar',
          position: 'left',
          label: 'Documentation',
        },
        {
          to: '/roadmap',
          label: 'Roadmap',
          position: 'left',
        },
        {
          href: 'https://github.com/your-org/group0',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Learn',
          items: [
            {label: 'Getting Started', to: '/docs/getting-started/installation'},
            {label: 'OS Concepts', to: '/docs/concepts/threads-and-processes'},
            {label: 'Project Guides', to: '/docs/projects/threads/overview'},
          ],
        },
        {
          title: 'Deep Dives',
          items: [
            {label: 'Context Switching', to: '/docs/deep-dives/context-switch-assembly'},
            {label: 'Virtual Memory', to: '/docs/deep-dives/page-fault-handler'},
            {label: 'File System', to: '/docs/deep-dives/wal'},
          ],
        },
        {
          title: 'More',
          items: [
            {label: 'Roadmap', to: '/roadmap'},
            {label: 'GitHub', href: 'https://github.com/your-org/group0'},
            {label: 'Changelog', to: '/docs/roadmap/changelog'},
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} PintOS Project. Built with Docusaurus.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['c', 'asm', 'bash', 'makefile'],
    },
    mermaid: {
      theme: {light: 'default', dark: 'dark'},
    },
    tableOfContents: {
      minHeadingLevel: 2,
      maxHeadingLevel: 4,
    },
    colorMode: {
      defaultMode: 'light',
      disableSwitch: false,
      respectPrefersColorScheme: true,
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
