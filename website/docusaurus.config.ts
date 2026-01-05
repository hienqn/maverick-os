import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'MaverickOS',
  tagline: 'Learn Operating Systems Through Code',
  favicon: 'img/favicon.ico',

  url: 'https://hienqn.github.io',
  baseUrl: '/maverick-os/',

  organizationName: 'hienqn',
  projectName: 'maverick-os',

  onBrokenLinks: 'warn',

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
        docs: false,
        blog: {
          showReadingTime: true,
          blogTitle: 'MaverickOS Blog',
          blogDescription: 'Learn OS concepts through interactive visualizations',
          blogSidebarTitle: 'Recent Posts',
          blogSidebarCount: 10,
          postsPerPage: 9,
          feedOptions: {
            type: null,
          },
          tagsBasePath: 'tags',
          routeBasePath: 'blog',
        },
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/pintos-social-card.png',
    navbar: {
      title: 'MaverickOS',
      logo: {
        alt: 'MaverickOS Logo',
        src: 'img/logo.svg',
      },
      items: [
        {
          to: '/blog',
          label: 'Blog',
          position: 'left',
        },
        {
          to: '/blog/tags',
          label: 'Topics',
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
          title: 'Blog',
          items: [
            {label: 'All Posts', to: '/blog'},
            {label: 'OS Concepts', to: '/blog/tags/os-concepts'},
            {label: 'Systems', to: '/blog/tags/systems'},
          ],
        },
        {
          title: 'More',
          items: [
            {label: 'GitHub', href: 'https://github.com/hienqn/maverick-os'},
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} MaverickOS`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['c', 'nasm', 'bash', 'makefile'],
    },
    mermaid: {
      theme: {light: 'default', dark: 'dark'},
    },
    colorMode: {
      defaultMode: 'light',
      disableSwitch: false,
      respectPrefersColorScheme: true,
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
