import Link from '@docusaurus/Link';
import Layout from '@theme/Layout';

import styles from './index.module.css';

export default function Home(): JSX.Element {
  return (
    <Layout
      title="Home"
      description="An educational operating system">
      <main className={styles.main}>
        <div className={styles.container}>
          <h1 className={styles.title}>MaverickOS</h1>
          <p className={styles.subtitle}>
            An educational operating system built from scratch.
          </p>
          <div className={styles.links}>
            <Link to="/blog" className={styles.link}>
              Blog
            </Link>
            <span className={styles.separator}>·</span>
            <Link to="https://github.com/hienqn/maverick-os" className={styles.link}>
              GitHub
            </Link>
          </div>
        </div>
      </main>
    </Layout>
  );
}
