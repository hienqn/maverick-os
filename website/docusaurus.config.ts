import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'PintOS Documentation',
  tagline: 'Learn Operating Systems Through Real Code',
  favicon: 'img/favicon.ico',

  url: 'https://hienqn.github.io',
  baseUrl: '/maverick-os/',

  organizationName: 'hienqn',
  projectName: 'maverick-os',

  onBrokenLinks: 'warn',
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
          editUrl: 'https://github.com/hienqn/maverick-os/tree/main/website/',
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
          href: 'https://github.com/hienqn/maverick-os',
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
          title: 'Documentation',
          items: [
            {label: 'Architecture', to: '/docs/architecture/overview'},
            {label: 'Context Switching', to: '/docs/concepts/context-switching'},
            {label: 'Changelog', to: '/docs/roadmap/changelog'},
          ],
        },
        {
          title: 'More',
          items: [
            {label: 'Roadmap', to: '/roadmap'},
            {label: 'GitHub', href: 'https://github.com/hienqn/maverick-os'},
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} Maverick OS. Built with Docusaurus.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['c', 'nasm', 'bash', 'makefile'],
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
